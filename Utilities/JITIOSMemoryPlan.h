#pragma once

#include "util/types.hpp"

#include <limits>

namespace rpcs3::ios::jit
{
struct code_allocation final
{
	usz offset = 0;
	usz committed_offset = 0;
	usz committed_size = 0;

	bool needs_commit() const noexcept
	{
		return committed_size != 0;
	}
};

// Plans suballocations inside one reserved RX range. Every returned allocation
// is contained by a single committed range so it can be translated to one
// contiguous writable alias.
class code_allocation_plan final
{
public:
	code_allocation_plan(usz reservation_size, usz commit_granularity) noexcept
		: m_reservation_size(reservation_size)
		, m_commit_granularity(commit_granularity)
	{
	}

	bool allocate(usz size, usz alignment, code_allocation& result) noexcept
	{
		result = {};
		if (!size || !is_power_of_two(alignment) || !is_power_of_two(m_commit_granularity))
		{
			return false;
		}

		usz allocation_offset = 0;
		usz allocation_size = 0;
		usz allocation_end = 0;
		if (!align_up(m_position, alignment, allocation_offset) ||
			!align_up(size, alignment, allocation_size) ||
			!checked_add(allocation_offset, allocation_size, allocation_end))
		{
			return false;
		}

		if (!m_has_mapping || allocation_offset < m_mapping_begin || allocation_end > m_mapping_end)
		{
			usz committed_offset = 0;
			if (!align_up(m_position, m_commit_granularity, committed_offset) ||
				!align_up(committed_offset, alignment, allocation_offset) ||
				!checked_add(allocation_offset, allocation_size, allocation_end))
			{
				return false;
			}

			usz committed_end = 0;
			if (!align_up(allocation_end, m_commit_granularity, committed_end) ||
				committed_end > m_reservation_size)
			{
				return false;
			}

			m_mapping_begin = committed_offset;
			m_mapping_end = committed_end;
			m_has_mapping = true;
			result.committed_offset = committed_offset;
			result.committed_size = committed_end - committed_offset;
		}
		else if (allocation_end > m_reservation_size)
		{
			return false;
		}

		m_position = allocation_end;
		result.offset = allocation_offset;
		return true;
	}

private:
	static bool is_power_of_two(usz value) noexcept
	{
		return value && !(value & (value - 1));
	}

	static bool checked_add(usz lhs, usz rhs, usz& result) noexcept
	{
		if (lhs > std::numeric_limits<usz>::max() - rhs)
		{
			return false;
		}
		result = lhs + rhs;
		return true;
	}

	static bool align_up(usz value, usz alignment, usz& result) noexcept
	{
		if (!is_power_of_two(alignment))
		{
			return false;
		}
		const usz mask = alignment - 1;
		if (value > std::numeric_limits<usz>::max() - mask)
		{
			return false;
		}
		result = (value + mask) & ~mask;
		return true;
	}

	usz m_reservation_size = 0;
	usz m_commit_granularity = 0;
	usz m_position = 0;
	usz m_mapping_begin = 0;
	usz m_mapping_end = 0;
	bool m_has_mapping = false;
};
}
