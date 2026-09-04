#pragma once

#include "util/types.hpp"

namespace rpcs3::ios::jit
{
inline constexpr usz mib = 1024 * 1024;
inline constexpr usz arena_min_capacity = 256 * mib;
inline constexpr usz arena_default_capacity = 384 * mib;
inline constexpr usz arena_max_capacity = 512 * mib;
inline constexpr usz arena_capacity_step = 64 * mib;
inline constexpr usz arena_prepare_chunk_size = 16 * mib;
inline constexpr u32 ppu_modules_per_jit = 25;

// Preparing an executable page causes debugserver to touch it, so size the
// one-time arena conservatively from physical RAM instead of always reserving
// RPCS3's desktop-sized 1 GiB code window. Expanded mode opts directly into
// the existing 512 MiB ceiling for titles that exhaust the standard arena,
// while lower-memory devices retain conservative sizing by default. Code and
// data receive the same capacity.
constexpr usz choose_arena_capacity(u64 physical_memory, bool expanded = false) noexcept
{
	if (expanded)
	{
		return arena_max_capacity;
	}

	usz capacity = arena_default_capacity;
	if (!physical_memory)
	{
		return arena_default_capacity;
	}

	const u64 candidate = (physical_memory / 16) & ~(static_cast<u64>(arena_capacity_step) - 1);
	if (candidate < arena_min_capacity)
	{
		capacity = arena_min_capacity;
	}
	else if (candidate > arena_max_capacity)
	{
		capacity = arena_max_capacity;
	}
	else
	{
		capacity = static_cast<usz>(candidate);
	}

	return capacity;
}

constexpr u32 arena_prepare_chunk_count(usz capacity) noexcept
{
	return capacity
		? static_cast<u32>(1 + (capacity - 1) / arena_prepare_chunk_size)
		: 0;
}

constexpr usz arena_prepare_chunk_length(usz capacity, u32 chunk_index) noexcept
{
	const usz offset = static_cast<usz>(chunk_index) * arena_prepare_chunk_size;
	if (offset >= capacity)
	{
		return 0;
	}

	const usz remaining = capacity - offset;
	return remaining < arena_prepare_chunk_size ? remaining : arena_prepare_chunk_size;
}

static_assert(choose_arena_capacity(0) == arena_default_capacity);
static_assert(choose_arena_capacity(4ull * 1024 * mib) == 256 * mib);
static_assert(choose_arena_capacity(4ull * 1024 * mib, true) == 512 * mib);
static_assert(choose_arena_capacity(6ull * 1024 * mib) == 384 * mib);
static_assert(choose_arena_capacity(6ull * 1024 * mib, true) == 512 * mib);
static_assert(choose_arena_capacity(8ull * 1024 * mib) == 512 * mib);
static_assert(choose_arena_capacity(8'000'000'000ull) == 448 * mib);
static_assert(choose_arena_capacity(8'000'000'000ull, true) == 512 * mib);
static_assert(arena_prepare_chunk_count(0) == 0);
static_assert(arena_prepare_chunk_count(448 * mib) == 28);
static_assert(arena_prepare_chunk_count(arena_max_capacity) == 32);
static_assert(arena_prepare_chunk_count(17 * mib) == 2);
static_assert(arena_prepare_chunk_length(17 * mib, 0) == 16 * mib);
static_assert(arena_prepare_chunk_length(17 * mib, 1) == 1 * mib);
static_assert(arena_prepare_chunk_length(17 * mib, 2) == 0);
static_assert(arena_max_capacity * 2 < 4ull * 1024 * 1024 * 1024);
}
