#pragma once

#include "util/types.hpp"

#include <limits>
#include <vector>

namespace rpcs3::ios::jit
{
struct reservation_range final
{
	uptr begin = 0;
	usz size = 0;
};

class reservation_registry final
{
public:
	bool contains(uptr begin, usz size) const noexcept
	{
		uptr end = 0;
		if (!checked_end(begin, size, end)) return false;
		for (const reservation_range& item : m_ranges)
		{
			if (begin >= item.begin && end <= item.begin + item.size) return true;
		}
		return false;
	}

	bool insert(uptr begin, usz size) noexcept
	{
		uptr end = 0;
		if (!checked_end(begin, size, end)) return false;
		for (const reservation_range& item : m_ranges)
		{
			const uptr item_end = item.begin + item.size;
			if (begin < item_end && item.begin < end) return false;
		}
		m_ranges.push_back({begin, size});
		return true;
	}

	void remove_contained(uptr begin, usz size) noexcept
	{
		uptr end = 0;
		if (!checked_end(begin, size, end)) return;
		for (auto iterator = m_ranges.begin(); iterator != m_ranges.end();)
		{
			if (iterator->begin >= begin && iterator->begin + iterator->size <= end)
			{
				iterator = m_ranges.erase(iterator);
			}
			else
			{
				++iterator;
			}
		}
	}

	usz size() const noexcept
	{
		return m_ranges.size();
	}

private:
	static bool checked_end(uptr begin, usz size, uptr& end) noexcept
	{
		if (!begin || !size || begin > std::numeric_limits<uptr>::max() - size) return false;
		end = begin + size;
		return true;
	}

	std::vector<reservation_range> m_ranges;
};
}
