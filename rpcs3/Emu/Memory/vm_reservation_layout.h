#pragma once

#include <cstdint>

namespace vm
{
	// Apple arm64 cores have 128-byte data cache lines. Keep independent
	// reservation stamps on separate lines without changing their guest hash.
#if defined(__APPLE__) && defined(__aarch64__)
	inline constexpr unsigned reservation_stride_shift = 0;
#else
	inline constexpr unsigned reservation_stride_shift = 1;
#endif
	inline constexpr unsigned reservation_stride = 128 >> reservation_stride_shift;
	inline constexpr unsigned reservation_table_size = 512 * reservation_stride;

	constexpr std::uint32_t reservation_offset(std::uint32_t addr)
	{
		return (addr & 0xff80) >> reservation_stride_shift;
	}
}
