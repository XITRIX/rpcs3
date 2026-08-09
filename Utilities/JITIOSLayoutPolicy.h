#pragma once

#include "util/types.hpp"

namespace rpcs3::ios::jit
{
inline constexpr usz mib = 1024 * 1024;
inline constexpr usz arena_min_capacity = 256 * mib;
inline constexpr usz arena_default_capacity = 384 * mib;
inline constexpr usz arena_max_capacity = 512 * mib;
inline constexpr usz arena_capacity_step = 64 * mib;
inline constexpr u32 ppu_modules_per_jit = 25;

// Preparing an executable page causes debugserver to touch it, so size the
// one-time arena conservatively from physical RAM instead of always reserving
// RPCS3's desktop-sized 1 GiB code window. A 4/6/8 GiB device receives a
// 256/384/512 MiB code arena plus an equally sized demand-paged data arena.
constexpr usz choose_arena_capacity(u64 physical_memory) noexcept
{
	if (!physical_memory)
	{
		return arena_default_capacity;
	}

	const u64 candidate = (physical_memory / 16) & ~(static_cast<u64>(arena_capacity_step) - 1);
	if (candidate < arena_min_capacity)
	{
		return arena_min_capacity;
	}
	if (candidate > arena_max_capacity)
	{
		return arena_max_capacity;
	}
	return static_cast<usz>(candidate);
}

static_assert(choose_arena_capacity(0) == arena_default_capacity);
static_assert(choose_arena_capacity(4ull * 1024 * mib) == 256 * mib);
static_assert(choose_arena_capacity(6ull * 1024 * mib) == 384 * mib);
static_assert(choose_arena_capacity(8ull * 1024 * mib) == 512 * mib);
static_assert(arena_max_capacity * 2 < 4ull * 1024 * 1024 * 1024);
}
