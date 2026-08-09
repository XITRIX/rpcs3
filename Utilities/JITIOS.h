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
// Reserve address space without consuming iOS executable-map capacity. Before
// debugger preparation, commit_reserved() replaces each requested subrange
// with a stable RX mapping and creates its shared RW alias.
void* reserve_address_space(usz size, usz executable_size) noexcept;
bool commit(void* executable, usz size) noexcept;
bool commit_reserved(void* executable, usz size) noexcept;
void* writable(const void* executable, usz size = 1) noexcept;
void flush(const void* executable, usz size) noexcept;
void release(void* executable, usz size) noexcept;
const char* last_error() noexcept;
}
