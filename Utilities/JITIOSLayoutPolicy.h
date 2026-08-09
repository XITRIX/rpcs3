#pragma once

#include "util/types.hpp"

namespace rpcs3::ios::jit
{
// PPU compilation is split into small object fragments before each retained
// MCJIT instance links them. Keeping a desktop-sized 256 MiB code and 256 MiB
// data window for every firmware PRX quickly exhausts iOS's userspace mapping
// range even though almost all of each window remains untouched.
inline constexpr usz llvm_region_capacity = 32 * 1024 * 1024;
inline constexpr usz llvm_layout_size = llvm_region_capacity * 2;
inline constexpr u32 ppu_modules_per_jit = 25;

static_assert(llvm_region_capacity >= 2 * 1024 * 1024);
static_assert((llvm_region_capacity & (llvm_region_capacity - 1)) == 0);
static_assert(llvm_layout_size < 4ull * 1024 * 1024 * 1024);
}
