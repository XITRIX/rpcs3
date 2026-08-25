#include "JITIOS.h"
#include "JITArenaAllocator.h"
#include "JITIOSLayoutPolicy.h"

#if !defined(RPCS3_IOS)
#error "JITIOS.cpp is only available in the Apple mobile frontend"
#endif

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>

#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <mach/vm_statistics.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <sys/ucontext.h>
#include <unistd.h>

extern "C" int csops(pid_t pid, unsigned int ops, void* user_address, size_t user_size);

namespace
{
constexpr u32 breakpoint_instruction = 0xd4200000u |
	(static_cast<u32>(rpcs3::ios::jit::breakpoint_immediate) << 5);
constexpr unsigned int cs_ops_status = 0;
constexpr u32 cs_debugged = 0x10000000u;

struct arena_state
{
	u8* code = nullptr;
	u8* writable_code = nullptr;
	u8* data = nullptr;
	usz capacity = 0;
	u32 preparation_chunks = 0;
	rpcs3::ios::jit::arena_allocator code_allocator;
	rpcs3::ios::jit::arena_allocator data_allocator;
	usz runtime_code_bytes = 0;
	usz runtime_data_bytes = 0;
	usz live_code_bytes = 0;
	usz live_data_bytes = 0;
	usz peak_code_bytes = 0;
	usz peak_data_bytes = 0;
	rpcs3::ios::jit::arena_backend backend = rpcs3::ios::jit::arena_backend::legacy_debugger;
	bool prepared = false;
	bool sealed = false;
};

std::mutex g_protocol_mutex;
std::mutex g_arena_mutex;
std::mutex g_error_mutex;
arena_state g_arena;
std::string g_last_error;
struct sigaction g_previous_trap_action{};

// Keep the stable code/data layout out of the anonymous heap VM range. XNU
// reserves tags 240-255 for application-specific mappings.
constexpr int jit_vm_tag = VM_MAKE_TAG(VM_MEMORY_APPLICATION_SPECIFIC_1);

void set_error(std::string message) noexcept
{
	std::lock_guard lock(g_error_mutex);
	g_last_error = std::move(message);
}

void forward_trap(int signal, siginfo_t* info, void* context)
{
	if ((g_previous_trap_action.sa_flags & SA_SIGINFO) && g_previous_trap_action.sa_sigaction)
	{
		g_previous_trap_action.sa_sigaction(signal, info, context);
		return;
	}

	if (g_previous_trap_action.sa_handler == SIG_IGN)
	{
		return;
	}

	if (g_previous_trap_action.sa_handler && g_previous_trap_action.sa_handler != SIG_DFL)
	{
		g_previous_trap_action.sa_handler(signal);
		return;
	}

	::sigaction(SIGTRAP, &g_previous_trap_action, nullptr);
	::raise(SIGTRAP);
}

void trap_fallback(int signal, siginfo_t* info, void* raw_context)
{
	auto* context = static_cast<ucontext_t*>(raw_context);
	if (!context || !context->uc_mcontext)
	{
		forward_trap(signal, info, raw_context);
		return;
	}

	auto& state = context->uc_mcontext->__ss;
	const uptr pc = static_cast<uptr>(__darwin_arm_thread_state64_get_pc(state));
	const u32 instruction = pc ? *reinterpret_cast<const u32*>(pc) : 0;
	const u64 command = state.__x[16];
	if (instruction != breakpoint_instruction ||
		(command != rpcs3::ios::jit::command_detach &&
			command != rpcs3::ios::jit::command_prepare_region))
	{
		forward_trap(signal, info, raw_context);
		return;
	}

	// A missing debugger is reported by command 1 returning zero. Command 0 is
	// intentionally a no-op in the fallback because there is nothing to detach.
	state.__x[0] = 0;
	__darwin_arm_thread_state64_set_pc_fptr(state, reinterpret_cast<void*>(pc + sizeof(u32)));
}

extern "C" __attribute__((naked, noinline, optnone))
u64 rpcs3_ios_jit26_protocol_call(u64, const void*, usz)
{
	__asm__ volatile(
		"mov x16, x0\n"
		"mov x0, x1\n"
		"mov x1, x2\n"
		"brk #0xf00d\n"
		"ret\n");
}

u64 protocol_call(u64 command, const void* address, usz size, bool* issued = nullptr) noexcept
{
	std::lock_guard lock(g_protocol_mutex);
	if (issued)
	{
		*issued = false;
	}

	struct sigaction fallback{};
	sigemptyset(&fallback.sa_mask);
	fallback.sa_sigaction = &trap_fallback;
	fallback.sa_flags = SA_SIGINFO;

	if (::sigaction(SIGTRAP, &fallback, &g_previous_trap_action) != 0)
	{
		set_error("Unable to install the scoped Universal JIT trap handler");
		return 0;
	}

	if (issued)
	{
		*issued = true;
	}
	const u64 result = rpcs3_ios_jit26_protocol_call(command, address, size);
	::sigaction(SIGTRAP, &g_previous_trap_action, nullptr);
	return result;
}

usz page_size() noexcept
{
	static const usz value = static_cast<usz>(::getpagesize());
	return value;
}

bool contains(const u8* base, usz capacity, const void* address, usz size, usz& offset) noexcept
{
	if (!base || !address || !size)
	{
		return false;
	}

	const uptr begin = reinterpret_cast<uptr>(base);
	const uptr value = reinterpret_cast<uptr>(address);
	if (value < begin || value - begin > capacity || size > capacity - (value - begin))
	{
		return false;
	}

	offset = static_cast<usz>(value - begin);
	return true;
}

u64 physical_memory_size() noexcept
{
	u64 value = 0;
	size_t size = sizeof(value);
	return ::sysctlbyname("hw.memsize", &value, &size, nullptr, 0) == 0 ? value : 0;
}

void discard_layout(u8* layout, usz total_size, vm_address_t writable_alias, usz capacity) noexcept
{
	if (writable_alias)
	{
		::vm_deallocate(mach_task_self(), writable_alias, static_cast<vm_size_t>(capacity));
	}
	if (layout)
	{
		::munmap(layout, total_size);
	}
}

void update_live_bytes(bool executable, usz amount) noexcept
{
	usz& live = executable ? g_arena.live_code_bytes : g_arena.live_data_bytes;
	usz& peak = executable ? g_arena.peak_code_bytes : g_arena.peak_data_bytes;
	live += amount;
	peak = std::max(peak, live);
}

rpcs3::ios::jit::arena_backend current_backend() noexcept
{
	if (__builtin_available(iOS 26.0, visionOS 26.0, *))
	{
		return rpcs3::ios::jit::arena_backend::universal_mirrored;
	}
	return rpcs3::ios::jit::arena_backend::legacy_debugger;
}

bool legacy_debugger_is_ready() noexcept
{
	int status = 0;
	if (::csops(::getpid(), cs_ops_status, &status, sizeof(status)) != 0)
	{
		set_error("Unable to inspect the legacy debugger JIT state: " + std::string{std::strerror(errno)});
		return false;
	}
	if ((static_cast<u32>(status) & cs_debugged) == 0)
	{
		set_error("StikDebug has not enabled JIT for this process");
		return false;
	}
	return true;
}
}

namespace rpcs3::ios::jit
{
bool is_ready() noexcept
{
	{
		std::lock_guard lock(g_arena_mutex);
		if (g_arena.prepared)
		{
			return true;
		}
	}

	if (current_backend() == arena_backend::legacy_debugger)
	{
		return legacy_debugger_is_ready();
	}

	const usz length = page_size();
	void* const probe = ::mmap(nullptr, length, PROT_READ | PROT_EXEC, MAP_PRIVATE | MAP_ANON, jit_vm_tag, 0);
	if (probe == MAP_FAILED)
	{
		set_error("Unable to reserve the Universal JIT readiness page: " + std::string{std::strerror(errno)});
		return false;
	}

	const uptr expected = reinterpret_cast<uptr>(probe);
	const u64 response = protocol_call(command_prepare_region, probe, length);
	::munmap(probe, length);
	if (response != expected)
	{
		set_error("StikDebug's Universal JIT script is not attached or did not prepare the readiness page");
		return false;
	}

	return true;
}

bool prepare_arena() noexcept
{
	std::lock_guard lock(g_arena_mutex);
	if (g_arena.prepared)
	{
		return true;
	}

	const arena_backend backend = current_backend();
	if (backend == arena_backend::legacy_debugger && !legacy_debugger_is_ready())
	{
		return false;
	}

	const usz capacity = choose_arena_capacity(physical_memory_size());
	if (!capacity || capacity > std::numeric_limits<usz>::max() / 2)
	{
		set_error("Invalid JIT arena capacity");
		return false;
	}

	const usz total_size = capacity * 2;
	auto* const layout = static_cast<u8*>(::mmap(nullptr, total_size, PROT_NONE,
		MAP_PRIVATE | MAP_ANON, jit_vm_tag, 0));
	if (layout == MAP_FAILED)
	{
		set_error("Unable to reserve the JIT arena layout: " + std::string{std::strerror(errno)});
		return false;
	}

	const int initial_code_protection = backend == arena_backend::universal_mirrored
		? PROT_READ | PROT_EXEC
		: PROT_READ | PROT_WRITE;
	void* const code = ::mmap(layout, capacity, initial_code_protection,
		MAP_FIXED | MAP_PRIVATE | MAP_ANON, jit_vm_tag, 0);
	if (code != layout)
	{
		const std::string detail = std::strerror(errno);
		discard_layout(layout, total_size, 0, capacity);
		set_error("Unable to map the JIT code arena: " + detail);
		return false;
	}

	void* const data = ::mmap(layout + capacity, capacity, PROT_READ | PROT_WRITE,
		MAP_FIXED | MAP_PRIVATE | MAP_ANON, jit_vm_tag, 0);
	if (data != layout + capacity)
	{
		const std::string detail = std::strerror(errno);
		discard_layout(layout, total_size, 0, capacity);
		set_error("Unable to map the JIT data arena: " + detail);
		return false;
	}

	u32 preparation_chunks = 0;
	if (backend == arena_backend::universal_mirrored)
	{
		preparation_chunks = arena_prepare_chunk_count(capacity);
		for (u32 chunk_index = 0; chunk_index < preparation_chunks; ++chunk_index)
		{
			const usz offset = static_cast<usz>(chunk_index) * arena_prepare_chunk_size;
			const usz chunk_length = arena_prepare_chunk_length(capacity, chunk_index);
			u8* const chunk = layout + offset;
			if (!chunk_length || protocol_call(command_prepare_region, chunk, chunk_length) != reinterpret_cast<uptr>(chunk))
			{
				discard_layout(layout, total_size, 0, capacity);
				set_error("The debugger did not prepare Universal JIT arena chunk " +
					std::to_string(chunk_index + 1) + " of " + std::to_string(preparation_chunks));
				return false;
			}
		}
	}

	vm_address_t alias = 0;
	vm_prot_t current_protection = VM_PROT_NONE;
	vm_prot_t maximum_protection = VM_PROT_NONE;
	const kern_return_t remap_result = ::vm_remap(
		mach_task_self(),
		&alias,
		static_cast<vm_size_t>(capacity),
		0,
		VM_FLAGS_ANYWHERE,
		mach_task_self(),
		static_cast<vm_address_t>(reinterpret_cast<uptr>(layout)),
		false,
		&current_protection,
		&maximum_protection,
		VM_INHERIT_SHARE);
	if (remap_result != KERN_SUCCESS)
	{
		discard_layout(layout, total_size, 0, capacity);
		set_error("mach_vm_remap failed while creating the arena's writable alias");
		return false;
	}

	if (::vm_protect(mach_task_self(), alias, static_cast<vm_size_t>(capacity), false,
		VM_PROT_READ | VM_PROT_WRITE) != KERN_SUCCESS)
	{
		discard_layout(layout, total_size, alias, capacity);
		set_error("mach_vm_protect failed for the arena's writable alias");
		return false;
	}

	// Below iOS/visionOS 26, debugger enablement permits the ordinary W-to-X
	// transition. Create the shared alias first so generated code can remain RX
	// at its relocation address while every later write uses the RW mapping.
	if (backend == arena_backend::legacy_debugger &&
		::mprotect(layout, capacity, PROT_READ | PROT_EXEC) != 0)
	{
		const std::string detail = std::strerror(errno);
		discard_layout(layout, total_size, alias, capacity);
		set_error("Unable to transition the legacy JIT arena from writable to executable: " + detail);
		return false;
	}

	g_arena.code_allocator.reset(capacity);
	g_arena.data_allocator.reset(capacity);

	g_arena.code = layout;
	g_arena.writable_code = reinterpret_cast<u8*>(alias);
	g_arena.data = layout + capacity;
	g_arena.capacity = capacity;
	g_arena.preparation_chunks = preparation_chunks;
	g_arena.backend = backend;
	g_arena.prepared = true;
	return true;
}

bool seal_arena() noexcept
{
	if (!prepare_arena())
	{
		return false;
	}

	arena_backend backend = arena_backend::legacy_debugger;
	{
		std::lock_guard lock(g_arena_mutex);
		if (g_arena.sealed)
		{
			return true;
		}
		g_arena.sealed = true;
		backend = g_arena.backend;
	}

	if (backend == arena_backend::legacy_debugger)
	{
		return true;
	}

	// StikDebug's built-in Universal script detaches on command 0. The scoped
	// fallback and Xcode stop hook consume the same command without detaching.
	bool issued = false;
	protocol_call(command_detach, nullptr, 0, &issued);
	if (!issued)
	{
		std::lock_guard lock(g_arena_mutex);
		g_arena.sealed = false;
		return false;
	}
	return true;
}

void* runtime_memory(bool executable) noexcept
{
	if (!prepare_arena())
	{
		return nullptr;
	}

	std::lock_guard lock(g_arena_mutex);
	return executable ? static_cast<void*>(g_arena.code) : static_cast<void*>(g_arena.data);
}

usz arena_capacity() noexcept
{
	if (!prepare_arena())
	{
		return 0;
	}

	std::lock_guard lock(g_arena_mutex);
	return g_arena.capacity;
}

bool claim_runtime(bool executable, usz offset, usz size) noexcept
{
	if (!size)
	{
		return true;
	}
	if (!prepare_arena())
	{
		return false;
	}

	std::lock_guard lock(g_arena_mutex);
	arena_allocator& allocator = executable ? g_arena.code_allocator : g_arena.data_allocator;
	arena_range allocation;
	if (!allocator.allocate_lowest(size, 1, allocation) || allocation.offset != offset)
	{
		if (allocation.size)
		{
			allocator.release(allocation.offset, allocation.size);
		}
		set_error("The JIT arena is exhausted or fragmented at the runtime boundary");
		return false;
	}

	usz& runtime = executable ? g_arena.runtime_code_bytes : g_arena.runtime_data_bytes;
	runtime += size;
	update_live_bytes(executable, size);
	return true;
}

void reset_runtime() noexcept
{
	std::lock_guard lock(g_arena_mutex);
	if (g_arena.runtime_code_bytes)
	{
		if (!g_arena.code_allocator.release(0, g_arena.runtime_code_bytes))
		{
			set_error("Unable to release the runtime code portion of the JIT arena");
			return;
		}
		g_arena.live_code_bytes -= g_arena.runtime_code_bytes;
		g_arena.runtime_code_bytes = 0;
	}
	if (g_arena.runtime_data_bytes)
	{
		if (!g_arena.data_allocator.release(0, g_arena.runtime_data_bytes))
		{
			set_error("Unable to release the runtime data portion of the JIT arena");
			return;
		}
		g_arena.live_data_bytes -= g_arena.runtime_data_bytes;
		g_arena.runtime_data_bytes = 0;
	}
}

void* allocate(bool executable, usz size, usz alignment) noexcept
{
	if (!prepare_arena())
	{
		return nullptr;
	}

	std::lock_guard lock(g_arena_mutex);
	arena_allocator& allocator = executable ? g_arena.code_allocator : g_arena.data_allocator;
	arena_range allocation;
	if (!allocator.allocate_highest(size, alignment, allocation))
	{
		set_error("The JIT arena is exhausted by a temporary allocation");
		return nullptr;
	}

	u8* const target = (executable ? g_arena.code : g_arena.data) + allocation.offset;
	u8* const storage = (executable ? g_arena.writable_code : g_arena.data) + allocation.offset;
	std::memset(storage, 0, allocation.size);
	update_live_bytes(executable, allocation.size);
	return target;
}

void release_allocation(bool executable, void* address, usz size) noexcept
{
	if (!address || !size)
	{
		return;
	}

	std::lock_guard lock(g_arena_mutex);
	usz offset = 0;
	u8* const base = executable ? g_arena.code : g_arena.data;
	if (!contains(base, g_arena.capacity, address, size, offset))
	{
		set_error("Attempted to release an address outside the JIT arena");
		return;
	}

	arena_allocator& allocator = executable ? g_arena.code_allocator : g_arena.data_allocator;
	if (!allocator.release(offset, size))
	{
		set_error("Attempted to release an invalid or overlapping JIT allocation");
		return;
	}

	usz& live = executable ? g_arena.live_code_bytes : g_arena.live_data_bytes;
	live = size <= live ? live - size : 0;
}

void* writable(const void* executable, usz size) noexcept
{
	std::lock_guard lock(g_arena_mutex);
	usz offset = 0;
	return contains(g_arena.code, g_arena.capacity, executable, size, offset)
		? static_cast<void*>(g_arena.writable_code + offset)
		: nullptr;
}

void flush(const void* executable, usz size) noexcept
{
	if (!executable || !size)
	{
		return;
	}

	void* const alias = writable(executable, size);
	::sys_dcache_flush(alias ? alias : const_cast<void*>(executable), size);
	::sys_icache_invalidate(const_cast<void*>(executable), size);
}

arena_statistics get_statistics() noexcept
{
	std::lock_guard lock(g_arena_mutex);
	return {
		g_arena.capacity,
		g_arena.preparation_chunks,
		g_arena.runtime_code_bytes,
		g_arena.runtime_data_bytes,
		g_arena.live_code_bytes,
		g_arena.live_data_bytes,
		g_arena.peak_code_bytes,
		g_arena.peak_data_bytes,
		g_arena.backend,
		g_arena.sealed,
	};
}

const char* last_error() noexcept
{
	thread_local std::string copy;
	std::lock_guard lock(g_error_mutex);
	copy = g_last_error;
	return copy.c_str();
}
}
