#pragma once

#include <algorithm>
#include <cstdint>

namespace vk
{
	inline constexpr std::uint64_t vram_budget_mib = 0x100000ull;

#ifdef RPCS3_IOS
	// MoltenVK exposes unified system memory as device-local memory. Leaving the
	// desktop VRAM limit in charge lets RSX consume most of the process's jetsam
	// allowance before its normal cache-pressure heuristics engage.
	inline constexpr std::uint64_t ios_vram_budget_min = 1024 * vram_budget_mib;
	inline constexpr std::uint64_t ios_vram_budget_max = 2048 * vram_budget_mib;
	inline constexpr std::uint64_t ios_vram_budget_divisor = 4;
#endif

	constexpr std::uint64_t get_requested_vram_allocation_limit(
		std::uint64_t reported_device_local_bytes,
		std::uint64_t configured_limit_bytes)
	{
		return std::min(reported_device_local_bytes, configured_limit_bytes);
	}

	constexpr std::uint64_t get_effective_vram_pressure_limit(
		std::uint64_t reported_device_local_bytes,
		std::uint64_t configured_limit_bytes)
	{
		const auto requested_limit = get_requested_vram_allocation_limit(
			reported_device_local_bytes,
			configured_limit_bytes);

#ifdef RPCS3_IOS
		const auto proportional_limit =
			(reported_device_local_bytes / ios_vram_budget_divisor / vram_budget_mib) * vram_budget_mib;
		const auto platform_limit = std::clamp(
			proportional_limit,
			ios_vram_budget_min,
			ios_vram_budget_max);

		return std::min(requested_limit, platform_limit);
#else
		return requested_limit;
#endif
	}

	constexpr std::uint64_t get_available_vram_budget(
		std::uint64_t budget_bytes,
		std::uint64_t usage_bytes)
	{
		return usage_bytes < budget_bytes ? budget_bytes - usage_bytes : 0;
	}
}
