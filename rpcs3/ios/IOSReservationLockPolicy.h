#pragma once

#include <cstdint>

namespace rpcs3::ios
{
	// SPU writers briefly spin to preserve low uncontended latency. Once a PPU
	// acknowledgement or overlapping range lock takes longer, occasionally yield
	// the host core so the PPU thread needed to complete the handshake can run.
	// Keep yields sparse: sched_yield is much more expensive than the ARM pause
	// hint when the producer is already making progress on another core.
	inline constexpr std::uint64_t reservation_lock_spin_iterations = 128;
	inline constexpr std::uint64_t reservation_lock_yield_interval = 32;

	constexpr bool reservation_lock_should_yield(std::uint64_t wait_iteration) noexcept
	{
		return wait_iteration >= reservation_lock_spin_iterations &&
			(wait_iteration % reservation_lock_yield_interval) == 0;
	}
}
