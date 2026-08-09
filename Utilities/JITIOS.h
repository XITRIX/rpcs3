#pragma once

#include "util/types.hpp"

namespace rpcs3::ios::jit
{
// StikDebug Universal JIT ABI: brk #0xf00d dispatches the command in x16.
// Command 0 detaches the debugger and command 1 prepares the x0/x1 executable
// range. RPCS3 prepares its complete process-lifetime arena before command 0.
inline constexpr u16 breakpoint_immediate = 0xf00d;
inline constexpr u64 command_detach = 0;
inline constexpr u64 command_prepare_region = 1;

struct arena_statistics
{
	usz capacity = 0;
	usz runtime_code_bytes = 0;
	usz runtime_data_bytes = 0;
	usz live_code_bytes = 0;
	usz live_data_bytes = 0;
	usz peak_code_bytes = 0;
	usz peak_data_bytes = 0;
	bool sealed = false;
};

bool is_ready() noexcept;
bool prepare_arena() noexcept;
bool seal_arena() noexcept;
void* runtime_memory(bool executable) noexcept;
usz arena_capacity() noexcept;
bool claim_runtime(bool executable, usz offset, usz size) noexcept;
void reset_runtime() noexcept;
void* allocate(bool executable, usz size, usz alignment) noexcept;
void release_allocation(bool executable, void* address, usz size) noexcept;
void* writable(const void* executable, usz size = 1) noexcept;
void flush(const void* executable, usz size) noexcept;
arena_statistics get_statistics() noexcept;
const char* last_error() noexcept;
}
