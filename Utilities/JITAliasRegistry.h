#pragma once

#include "util/types.hpp"

#include <limits>
#include <vector>

namespace rpcs3::ios::jit
{
struct alias_mapping final
{
	uptr executable = 0;
	uptr writable = 0;
	usz size = 0;
};

class alias_registry final
{
public:
	bool contains(uptr executable, usz size) const noexcept
	{
		uptr end = 0;
		if (!checked_end(executable, size, end)) return false;
		for (const auto& item : m_mappings)
		{
			if (executable >= item.executable && end <= item.executable + item.size) return true;
		}
		return false;
	}

	bool insert(uptr executable, uptr writable, usz size) noexcept
	{
		uptr executable_end = 0;
		uptr writable_end = 0;
		if (!executable || !writable || !checked_end(executable, size, executable_end) ||
			!checked_end(writable, size, writable_end))
		{
			return false;
		}

		for (const auto& item : m_mappings)
		{
			const uptr item_end = item.executable + item.size;
			if (executable < item_end && item.executable < executable_end) return false;
		}

		m_mappings.push_back({executable, writable, size});
		return true;
	}

	void* translate(uptr executable, usz size) const noexcept
	{
		uptr end = 0;
		if (!checked_end(executable, size, end)) return nullptr;
		for (const auto& item : m_mappings)
		{
			if (executable >= item.executable && end <= item.executable + item.size)
			{
				return reinterpret_cast<void*>(item.writable + executable - item.executable);
			}
		}
		return nullptr;
	}

	template <typename Callback>
	void remove_contained(uptr executable, usz size, Callback&& callback) noexcept
	{
		uptr end = 0;
		if (!checked_end(executable, size, end)) return;
		for (auto iterator = m_mappings.begin(); iterator != m_mappings.end();)
		{
			if (iterator->executable >= executable && iterator->executable + iterator->size <= end)
			{
				callback(*iterator);
				iterator = m_mappings.erase(iterator);
			}
			else
			{
				++iterator;
			}
		}
	}

	usz size() const noexcept { return m_mappings.size(); }

private:
	static bool checked_end(uptr begin, usz size, uptr& end) noexcept
	{
		if (!size || begin > std::numeric_limits<uptr>::max() - size) return false;
		end = begin + size;
		return true;
	}

	std::vector<alias_mapping> m_mappings;
};
}
