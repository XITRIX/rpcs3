#pragma once

#include <cstdint>

namespace vm
{
	// The range must be contained in guest memory. point is a 128-byte
	// reservation index, already translated through the shared-memory mirror.
	// Nonzero mirrors are 64 KiB aligned and outside the 32-bit guest address
	// space (vm::_page_map tags them with range_locked).
	template <typename SharedMemoryLookup>
	constexpr bool reservation_range_overlaps(std::uint64_t point, std::uint32_t address,
		std::uint32_t size, SharedMemoryLookup shared_memory)
	{
		const std::uint64_t end = std::uint64_t{address} + size;
		if (point < (std::uint64_t{1} << (32 - 7)))
		{
			// An unshared reservation can only overlap its own guest line.
			// Other pages either retain different guest addresses or translate
			// outside guest memory. Only the point's page needs an acquire load;
			// do not cache it, as sharing can change between invocations.
			if (!size || point < address / 128 || point > (end - 1) / 128)
			{
				return false;
			}

			return shared_memory(point >> (16 - 7)) == 0;
		}

		if (!size)
		{
			return false;
		}

		const auto first_line = std::uint64_t{address} / 128;
		const auto last_line = (end - 1) / 128;
		for (std::uint64_t page = address >> 16; page <= last_line / 512; ++page)
		{
			// Shared aliases retain their low 16 address bits. Compare pages
			// first, then check the reservation line within the original range.
			// A zero mirror cannot match a shared point. Every possible alias
			// still receives its own fresh acquire load, in ascending order.
			if ((shared_memory(page) >> 16) == (point / 512)) [[unlikely]]
			{
				const auto line = page * 512 + point % 512;
				if (line - first_line <= last_line - first_line)
				{
					return true;
				}
			}
		}

		return false;
	}
}
