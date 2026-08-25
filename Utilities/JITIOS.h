#pragma once

#include "util/types.hpp"

namespace rpcs3::ios::jit
{
// iOS 26+ uses StikDebug's Universal JIT ABI: brk #0xf00d dispatches the
// command in x16. Command 0 detaches the debugger and command 1 prepares the
// x0/x1 executable range. iOS 17.4-18.x uses the persistent debugger-enabled
// code-signing state and an ordinary W-to-X transition instead.
inline constexpr u16 breakpoint_immediate = 0xf00d;
inline constexpr u64 command_detach = 0;
inline constexpr u64 command_prepare_region = 1;

enum class arena_backend : u8
{
	legacy_debugger,
	universal_mirrored,
};

inline constexpr u32 universal_backend_ios_major = 26;

constexpr arena_backend backend_for_ios_major(u32 major_version) noexcept
{
	return major_version >= universal_backend_ios_major
		? arena_backend::universal_mirrored
		: arena_backend::legacy_debugger;
}

struct arena_statistics
{
	usz capacity = 0;
	u32 preparation_chunks = 0;
	usz runtime_code_bytes = 0;
	usz runtime_data_bytes = 0;
	usz live_code_bytes = 0;
	usz live_data_bytes = 0;
	usz peak_code_bytes = 0;
	usz peak_data_bytes = 0;
	arena_backend backend = arena_backend::legacy_debugger;
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
