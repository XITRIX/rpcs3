#pragma once

#include "util/types.hpp"
#include <string>

namespace rpcs3::ios::jit
{
// iOS/visionOS 26+ uses the Universal JIT ABI: brk #0xf00d dispatches the
// command in x16. Command 0 detaches the debugger and command 1 prepares the
// x0/x1 executable range. Earlier supported releases use the persistent
// debugger-enabled code-signing state and an ordinary W-to-X transition.
// Apple simulators use the same W-to-X alias layout under the host's policy,
// without device code-signing checks or Universal commands.
inline constexpr u16 breakpoint_immediate = 0xf00d;
inline constexpr u64 command_detach = 0;
inline constexpr u64 command_prepare_region = 1;

enum class arena_backend : u8
{
	disabled,
	legacy_debugger,
	universal_mirrored,
};

enum class memory_mode : u8
{
	automatic,
	arena,
	regular,
	invalid,
};

constexpr memory_mode resolve_memory_mode(memory_mode requested, arena_backend backend) noexcept
{
	if (requested == memory_mode::automatic)
	{
		return backend == arena_backend::universal_mirrored ? memory_mode::arena : memory_mode::regular;
	}
	return requested == memory_mode::regular && backend == arena_backend::universal_mirrored
		? memory_mode::invalid : requested;
}

// Selected before dlopen, since global constructors can emit executable code.
// Automatic uses regular allocations on Mac/Simulator/pre-26 iOS and the
// prepared arena on Universal JIT. A process cannot change modes after loading.
memory_mode selected_memory_mode() noexcept;
bool uses_regular_memory() noexcept;

// A desktop-style compiler-local code/data reservation. Pages and their RW
// aliases are mapped on demand; adjacent targets preserve ARM64 ADRP reach.
// Destruction releases the compiler's mappings, independently of other groups.
class regular_allocator
{
	void* m_group = nullptr;

public:
	regular_allocator() = default;
	~regular_allocator();
	regular_allocator(const regular_allocator&) = delete;
	regular_allocator& operator=(const regular_allocator&) = delete;
	void* allocate(bool executable, usz size, usz alignment) noexcept;
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
	usz data_capacity = 0;
	u32 preparation_requests = 0;
	usz runtime_code_bytes = 0;
	usz runtime_data_bytes = 0;
	usz live_code_bytes = 0;
	usz live_data_bytes = 0;
	usz free_code_bytes = 0;
	usz free_data_bytes = 0;
	usz largest_free_code_bytes = 0;
	usz largest_free_data_bytes = 0;
	usz peak_code_bytes = 0;
	usz peak_data_bytes = 0;
	usz mapped_code_bytes = 0;
	usz mapped_data_bytes = 0;
	memory_mode mode = memory_mode::arena;
	arena_backend backend = arena_backend::legacy_debugger;
	bool expanded = false;
	bool sealed = false;
};

// Latched from the wrapper's environment before any dylib constructors run.
bool is_jitless() noexcept;
bool is_ready() noexcept;
bool prepare_arena() noexcept;
bool prepare_arena(u32 expanded_capacity_mib) noexcept;
bool seal_arena() noexcept;
void* runtime_memory(bool executable) noexcept;
usz arena_capacity(bool executable = true) noexcept;
bool claim_runtime(bool executable, usz offset, usz size) noexcept;
void reset_runtime() noexcept;
void* allocate(bool executable, usz size, usz alignment) noexcept;
void release_allocation(bool executable, void* address, usz size) noexcept;
void* writable(const void* executable, usz size = 1) noexcept;
void flush(const void* executable, usz size) noexcept;
arena_statistics get_statistics() noexcept;
// Empty unless the process opted into the prepared-arena accounting experiment.
std::string no_footprint_diagnostics();
const char* last_error() noexcept;
}
