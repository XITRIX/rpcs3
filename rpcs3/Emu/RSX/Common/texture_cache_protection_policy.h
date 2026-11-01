#pragma once

#include <cstddef>

namespace rsx
{
	enum section_protection_strategy
	{
		lock,
		hash
	};

	constexpr std::size_t min_lockable_texture_data_size = 4096;

	// Signed AOT code uses the same guest-data fault handler without a JIT
	// arena. All paths require a known detached debugger; interpreter-only
	// sessions retain their existing hashing policy.
	constexpr bool allow_texture_write_tracking(bool requested,
		bool jit_ready, bool debugger_known_detached, bool signed_aot = false) noexcept
	{
		return requested && (jit_ready || signed_aot) && debugger_known_detached;
	}

	constexpr section_protection_strategy default_texture_protection_strategy(
		std::size_t memory_length, [[maybe_unused]] bool write_tracking = false) noexcept
	{
#ifdef RPCS3_IOS
		// Mach exceptions reach an attached debugger before RPCS3's signal
		// handler. Universal page-locking previously produced native execute faults;
		// it remains experimental and requires the explicit opt-in above. Hashing
		// is the default without changing native page protection.
		if (write_tracking && memory_length >= min_lockable_texture_data_size)
		{
			return section_protection_strategy::lock;
		}
		return section_protection_strategy::hash;
#else
		return memory_length < min_lockable_texture_data_size
			? section_protection_strategy::hash
			: section_protection_strategy::lock;
#endif
	}

	constexpr section_protection_strategy texture_protection_strategy_for_access(
		section_protection_strategy strategy,
		bool requires_no_access) noexcept
	{
		// GPU-owned ranges must remain inaccessible until their contents have
		// been synchronized back to guest memory. Hashing cannot replace that
		// ownership barrier.
		return requires_no_access ? section_protection_strategy::lock : strategy;
	}
}
