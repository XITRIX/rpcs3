#pragma once

#include <algorithm>
#include <cstdint>

namespace vm
{
	// The range must be contained in guest memory. point is a 128-byte
	// reservation index, already translated through the shared-memory mirror.
	template <typename SharedMemoryLookup>
	constexpr bool reservation_range_overlaps(std::uint64_t point, std::uint32_t address,
		std::uint32_t size, SharedMemoryLookup shared_memory)
	{
		const std::uint64_t end = std::uint64_t{address} + size;
		for (std::uint64_t current = address; current < end;)
		{
			// Advance to the NEXT 64 KiB boundary even when current is aligned.
			// Rounding current up would make an empty chunk, whose inclusive-end
			// subtraction underflows and falsely conflicts with other reservations.
			const auto next = std::min(end, (current | std::uint64_t{0xffff}) + 1);
			const auto chunk_size = next - current;
			auto mapped = current;
			if (const std::uint64_t mirror = shared_memory(current >> 16)) [[unlikely]]
			{
				mapped = (current & 0xffff) | mirror;
			}

			if (point - mapped / 128 <= (mapped + chunk_size - 1) / 128 - mapped / 128) [[unlikely]]
			{
				return true;
			}

			current = next;
		}

		return false;
	}
}
