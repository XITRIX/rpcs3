#pragma once

#include <algorithm>
#include <cstdint>

namespace vk
{
// The driver allocation limit applies to a single backing buffer. Replacing a
// full ring at its current capacity is supported; exceeding the limit is not.
constexpr std::uint64_t heap_growth_size(std::uint64_t current, std::uint64_t request,
	std::uint64_t policy_limit, std::uint64_t device_limit)
{
	const auto limit = device_limit ? std::min(policy_limit, device_limit) : policy_limit;
	if (!limit || request > limit) return 0;
	const auto needed = current > limit - request ? limit : current + request;
	constexpr std::uint64_t alignment = 64 * 1024 * 1024;
	if (needed > limit - std::min(limit, alignment - 1)) return limit;
	return std::min(limit, (needed + alignment - 1) & ~(alignment - 1));
}
}
