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

	constexpr section_protection_strategy default_texture_protection_strategy(
		std::size_t memory_length) noexcept
	{
#ifdef RPCS3_IOS
		// Mach exceptions reach an attached debugger before RPCS3's signal
		// handler. Hashing read-only texture sources avoids repeatedly stopping
		// the entire iOS process while retaining the existing cache-validation
		// semantics used for small ranges on other platforms.
		(void)memory_length;
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
