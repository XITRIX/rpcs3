#pragma once

#include "util/types.hpp"

#include <algorithm>
#include <limits>
#include <vector>

namespace rpcs3::ios::jit
{
struct arena_range
{
	usz offset = 0;
	usz size = 0;
};

// Offset-only allocator for the process-lifetime iOS JIT arenas. Permanent
// runtime allocations grow from the bottom while recyclable LLVM allocations
// grow from the top, so neither side needs a fixed partition.
class arena_allocator
{
public:
	arena_allocator() = default;
	explicit arena_allocator(usz capacity)
	{
		reset(capacity);
	}

	void reset(usz capacity)
	{
		m_capacity = capacity;
		m_free.clear();
		if (capacity)
		{
			m_free.push_back({0, capacity});
		}
	}

	bool allocate_lowest(usz size, usz alignment, arena_range& result)
	{
		if (!valid_request(size, alignment))
		{
			return false;
		}

		for (usz index = 0; index < m_free.size(); index++)
		{
			const arena_range available = m_free[index];
			const usz offset = align_up(available.offset, alignment);
			if (offset < available.offset || offset > available.offset + available.size ||
				size > available.offset + available.size - offset)
			{
				continue;
			}

			consume(index, offset, size);
			result = {offset, size};
			return true;
		}

		return false;
	}

	bool allocate_highest(usz size, usz alignment, arena_range& result)
	{
		if (!valid_request(size, alignment))
		{
			return false;
		}

		for (usz reverse = m_free.size(); reverse; reverse--)
		{
			const usz index = reverse - 1;
			const arena_range available = m_free[index];
			const usz end = available.offset + available.size;
			if (size > available.size)
			{
				continue;
			}

			const usz offset = (end - size) & ~(alignment - 1);
			if (offset < available.offset)
			{
				continue;
			}

			consume(index, offset, size);
			result = {offset, size};
			return true;
		}

		return false;
	}

	bool release(usz offset, usz size)
	{
		if (!size || offset > m_capacity || size > m_capacity - offset)
		{
			return false;
		}

		const auto next = std::lower_bound(m_free.begin(), m_free.end(), offset,
			[](const arena_range& item, usz value)
			{
				return item.offset < value;
			});
		const usz index = static_cast<usz>(next - m_free.begin());
		if (next != m_free.end() && offset + size > next->offset)
		{
			return false;
		}
		if (index && m_free[index - 1].offset + m_free[index - 1].size > offset)
		{
			return false;
		}

		m_free.insert(next, {offset, size});
		coalesce(index);
		return true;
	}

	usz capacity() const noexcept
	{
		return m_capacity;
	}

	usz free_bytes() const noexcept
	{
		usz result = 0;
		for (const arena_range& item : m_free)
		{
			result += item.size;
		}
		return result;
	}

private:
	static bool is_power_of_two(usz value) noexcept
	{
		return value && !(value & (value - 1));
	}

	bool valid_request(usz size, usz alignment) const noexcept
	{
		return size && size <= m_capacity && is_power_of_two(alignment);
	}

	static usz align_up(usz value, usz alignment) noexcept
	{
		if (value > std::numeric_limits<usz>::max() - (alignment - 1))
		{
			return std::numeric_limits<usz>::max();
		}
		return (value + alignment - 1) & ~(alignment - 1);
	}

	void consume(usz index, usz offset, usz size)
	{
		const arena_range available = m_free[index];
		const usz prefix = offset - available.offset;
		const usz suffix_offset = offset + size;
		const usz suffix = available.offset + available.size - suffix_offset;

		if (prefix && suffix)
		{
			m_free[index] = {available.offset, prefix};
			m_free.insert(m_free.begin() + index + 1, {suffix_offset, suffix});
		}
		else if (prefix)
		{
			m_free[index] = {available.offset, prefix};
		}
		else if (suffix)
		{
			m_free[index] = {suffix_offset, suffix};
		}
		else
		{
			m_free.erase(m_free.begin() + index);
		}
	}

	void coalesce(usz index)
	{
		if (index && m_free[index - 1].offset + m_free[index - 1].size == m_free[index].offset)
		{
			m_free[index - 1].size += m_free[index].size;
			m_free.erase(m_free.begin() + index);
			index--;
		}

		if (index + 1 < m_free.size() &&
			m_free[index].offset + m_free[index].size == m_free[index + 1].offset)
		{
			m_free[index].size += m_free[index + 1].size;
			m_free.erase(m_free.begin() + index + 1);
		}
	}

	usz m_capacity = 0;
	std::vector<arena_range> m_free;
};
}
