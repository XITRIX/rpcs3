#pragma once

#include "util/types.hpp"

namespace rpcs3::ios::jit
{
// StikDebug Universal JIT ABI: brk #0xf00d dispatches the command in x16.
// Command 1 prepares the x0/x1 address range for executable mappings.
inline constexpr u16 breakpoint_immediate = 0xf00d;
inline constexpr u64 command_prepare_region = 1;

bool is_ready() noexcept;
void* reserve(usz size) noexcept;
bool commit(void* executable, usz size) noexcept;
void* writable(const void* executable, usz size = 1) noexcept;
void flush(const void* executable, usz size) noexcept;
void release(void* executable, usz size) noexcept;
const char* last_error() noexcept;
}
