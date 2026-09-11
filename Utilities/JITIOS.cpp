#include "JITIOS.h"
#include "JITArenaAllocator.h"
#include "JITIOSLayoutPolicy.h"

#if !defined(RPCS3_IOS)
#error "JITIOS.cpp is only available in the Apple mobile frontend"
#endif

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>

#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <mach/vm_statistics.h>
#include <os/log.h>
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
	usz data_capacity = 0;
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
	bool expanded = false;
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
constexpr char expanded_jit_arena_environment[] = "RPCS3_IOS_EXPANDED_JIT_ARENA";
constexpr vm_address_t arena_address_begin = 0x1'0000'0000;
constexpr vm_address_t arena_address_end = 0x10'0000'0000;
constexpr vm_size_t arena_address_step = 64 * 1024 * 1024;

u8* reserve_arena_layout(usz size, vm_address_t begin = arena_address_begin,
	vm_address_t end = arena_address_end) noexcept
{
	// An unconstrained reservation can land in the extended high-address range,
	// where Universal preparation can acknowledge RX pages that still fault on
	// execution. Search a bounded low address window, leaving shared libraries
	// and RPCS3's guest reservations in place.
	// VM_FLAGS_FIXED without VM_FLAGS_OVERWRITE fails on occupied addresses;
	// only the successfully reserved range may later receive MAP_FIXED mappings.
	if (!size || begin < arena_address_begin || end > arena_address_end || begin >= end || size > end - begin)
	{
		return nullptr;
	}

	begin = (begin + arena_address_step - 1) & ~(arena_address_step - 1);
	for (vm_address_t candidate = begin; candidate <= end - size; candidate += arena_address_step)
	{
		usz reserved = 0;
		while (reserved < size)
		{
			const usz length = std::min(size - reserved, rpcs3::ios::jit::arena_prepare_chunk_size);
			vm_address_t address = candidate + reserved;
			if (::vm_map(mach_task_self(), &address, static_cast<vm_size_t>(length), 0,
				VM_FLAGS_FIXED | jit_vm_tag, MACH_PORT_NULL, 0, false,
				VM_PROT_NONE, VM_PROT_ALL, VM_INHERIT_DEFAULT) != KERN_SUCCESS)
			{
				break;
			}
			reserved += length;
		}
		if (reserved == size)
		{
			return reinterpret_cast<u8*>(candidate);
		}
		if (reserved)
		{
			::vm_deallocate(mach_task_self(), candidate, static_cast<vm_size_t>(reserved));
		}
	}
	return nullptr;
}

u8* reserve_code_data_layout(usz code_capacity, u8*& data, usz& data_capacity) noexcept
{
	using namespace rpcs3::ios::jit;
	data_capacity = 0;
	data = nullptr;
	if (code_capacity < arena_min_capacity || code_capacity > arena_max_capacity)
	{
		return nullptr;
	}

	// Prefer the original adjacent, equal-capacity layout when space permits.
	if (u8* code = reserve_arena_layout(code_capacity * 2))
	{
		data = code + code_capacity;
		data_capacity = code_capacity;
		return code;
	}

	// Shared libraries/stacks can split the remaining low address space. Keep
	// the requested code capacity and search separate nearby gaps for data.
	// The combined span must fit within 4 GiB so every code/data page pair is
	// in signed ADRP reach. Never place data in the distant writable-alias range.
	constexpr vm_size_t reach = 0x1'0000'0000;
	vm_address_t next_code = arena_address_begin;
	while (u8* code = reserve_arena_layout(code_capacity, next_code))
	{
		const auto code_address = reinterpret_cast<vm_address_t>(code);
		const vm_address_t data_begin = std::max(arena_address_begin, code_address + code_capacity - reach);
		const vm_address_t data_end = std::min(arena_address_end, code_address + reach);
		data_capacity = code_capacity;
		for (;;)
		{
			if ((data = reserve_arena_layout(data_capacity, data_begin, data_end)))
			{
				return code;
			}
			if (data_capacity == arena_min_capacity)
			{
				break;
			}
			data_capacity = std::max(arena_min_capacity,
				(data_capacity / 2) & ~(arena_capacity_step - 1));
		}
		::vm_deallocate(mach_task_self(), code_address, static_cast<vm_size_t>(code_capacity));
		next_code = code_address + arena_address_step;
	}
	data_capacity = 0;
	return nullptr;
}

bool map_arena_region(u8* address, usz size, int protection) noexcept
{
	for (usz offset = 0; offset < size;)
	{
		const usz length = std::min(size - offset, rpcs3::ios::jit::arena_prepare_chunk_size);
		if (::mmap(address + offset, length, protection,
			MAP_FIXED | MAP_PRIVATE | MAP_ANON, jit_vm_tag, 0) != address + offset)
		{
			return false;
		}
		offset += length;
	}
	return true;
}

u32 process_expanded_jit_arena_capacity() noexcept
{
	const char* const value = std::getenv(expanded_jit_arena_environment);
	return value ? rpcs3::ios::jit::parse_expanded_arena_capacity(value) : 0;
}

void set_error(std::string message) noexcept
{
	// Core constructors can fail before the frontend log callback is installed.
	os_log_error(OS_LOG_DEFAULT, "RPCS3 JIT: %{public}s", message.c_str());
	std::lock_guard lock(g_error_mutex);
	g_last_error = std::move(message);
}

// The caller holds g_arena_mutex, so read the allocator and counters directly
// instead of taking another arena snapshot while reporting the failed request.
void set_arena_exhaustion_error(bool executable, std::string_view allocation_kind,
	usz size, usz alignment, usz required_offset = std::numeric_limits<usz>::max()) noexcept
{
	const auto& allocator = executable ? g_arena.code_allocator : g_arena.data_allocator;
	const usz live = executable ? g_arena.live_code_bytes : g_arena.live_data_bytes;
	const usz runtime = executable ? g_arena.runtime_code_bytes : g_arena.runtime_data_bytes;
	const usz peak = executable ? g_arena.peak_code_bytes : g_arena.peak_data_bytes;
	std::string message = "JIT_ARENA_EXHAUSTED: ";
	if (!executable && g_arena.data_capacity < g_arena.capacity)
	{
		// Increasing executable capacity cannot recover a data reserve that was
		// constrained by address space. Do not trigger the expansion proposal.
		message = "JIT_DATA_ADDRESS_SPACE_EXHAUSTED: ";
	}
	message += executable ? "code" : "data";
	message += " arena could not satisfy ";
	message += allocation_kind;
	message += " (requested=" + std::to_string(size);
	message += " bytes, alignment=" + std::to_string(alignment);
	if (required_offset != std::numeric_limits<usz>::max())
	{
		message += ", required_offset=" + std::to_string(required_offset);
	}
	message += ", free=" + std::to_string(allocator.free_bytes());
	message += " bytes, largest_free=" + std::to_string(allocator.largest_free_bytes());
	message += " bytes, live=" + std::to_string(live);
	message += " bytes, runtime=" + std::to_string(runtime);
	message += " bytes, peak=" + std::to_string(peak);
	message += " bytes, capacity=" + std::to_string(allocator.capacity()) + " bytes)";
	set_error(std::move(message));
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

void discard_layout(u8* code, usz capacity, u8* data, usz data_capacity, vm_address_t writable_alias) noexcept
{
	if (writable_alias)
	{
		::vm_deallocate(mach_task_self(), writable_alias, static_cast<vm_size_t>(capacity));
	}
	if (code)
	{
		::munmap(code, capacity);
	}
	if (data)
	{
		::munmap(data, data_capacity);
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
	return prepare_arena(process_expanded_jit_arena_capacity());
}

bool prepare_arena(u32 expanded_capacity_mib) noexcept
{
	std::lock_guard lock(g_arena_mutex);
	if (!valid_expanded_arena_capacity(expanded_capacity_mib))
	{
		set_error("Invalid JIT arena capacity; expected 0, 1, or 512–1024 MiB");
		return false;
	}
	const bool expanded = expanded_capacity_mib != 0;
	if (g_arena.prepared)
	{
		if (g_arena.expanded != expanded ||
			(expanded && g_arena.capacity != choose_arena_capacity(0, expanded_capacity_mib)))
		{
			set_error("JIT arena capacity policy changed after the arena was prepared; relaunch is required");
			return false;
		}
		return true;
	}

	const arena_backend backend = current_backend();
	if (backend == arena_backend::legacy_debugger && !legacy_debugger_is_ready())
	{
		return false;
	}

	const usz capacity = choose_arena_capacity(physical_memory_size(), expanded_capacity_mib);
	usz data_capacity = 0;
	u8* data = nullptr;
	u8* const layout = reserve_code_data_layout(capacity, data, data_capacity);
	if (!layout)
	{
		set_error("Unable to reserve the JIT arena layout in low virtual address space (code=" +
			std::to_string(capacity) + ", minimum data=" + std::to_string(arena_min_capacity) + " bytes)");
		return false;
	}

	const int initial_code_protection = backend == arena_backend::universal_mirrored
		? PROT_READ | PROT_EXEC
		: PROT_READ | PROT_WRITE;
	if (!map_arena_region(layout, capacity, initial_code_protection))
	{
		const std::string detail = std::strerror(errno);
		discard_layout(layout, capacity, data, data_capacity, 0);
		set_error("Unable to map the JIT code arena: " + detail);
		return false;
	}

	if (!map_arena_region(data, data_capacity, PROT_READ | PROT_WRITE))
	{
		const std::string detail = std::strerror(errno);
		discard_layout(layout, capacity, data, data_capacity, 0);
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
			const u64 response = chunk_length ? protocol_call(command_prepare_region, chunk, chunk_length) : 0;
			if (!chunk_length || response != reinterpret_cast<uptr>(chunk))
			{
				discard_layout(layout, capacity, data, data_capacity, 0);
				set_error("The debugger did not prepare Universal JIT arena chunk " +
					std::to_string(chunk_index + 1) + " of " + std::to_string(preparation_chunks) +
					" (address=" + std::to_string(reinterpret_cast<uptr>(chunk)) +
					", length=" + std::to_string(chunk_length) + ", response=" + std::to_string(response) + ")");
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
		discard_layout(layout, capacity, data, data_capacity, 0);
		set_error("mach_vm_remap failed while creating the arena's writable alias");
		return false;
	}

	if (::vm_protect(mach_task_self(), alias, static_cast<vm_size_t>(capacity), false,
		VM_PROT_READ | VM_PROT_WRITE) != KERN_SUCCESS)
	{
		discard_layout(layout, capacity, data, data_capacity, alias);
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
		discard_layout(layout, capacity, data, data_capacity, alias);
		set_error("Unable to transition the legacy JIT arena from writable to executable: " + detail);
		return false;
	}

	g_arena.code_allocator.reset(capacity);
	g_arena.data_allocator.reset(data_capacity);

	g_arena.code = layout;
	g_arena.writable_code = reinterpret_cast<u8*>(alias);
	g_arena.data = data;
	g_arena.capacity = capacity;
	g_arena.data_capacity = data_capacity;
	g_arena.preparation_chunks = preparation_chunks;
	g_arena.backend = backend;
	g_arena.expanded = expanded;
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

usz arena_capacity(bool executable) noexcept
{
	if (!prepare_arena())
	{
		return 0;
	}

	std::lock_guard lock(g_arena_mutex);
	return executable ? g_arena.capacity : g_arena.data_capacity;
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
		set_arena_exhaustion_error(executable, "a runtime-boundary extension", size, 1, offset);
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
		set_arena_exhaustion_error(executable, "a temporary allocation", size, alignment);
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
	if (!contains(base, executable ? g_arena.capacity : g_arena.data_capacity, address, size, offset))
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
	arena_statistics result;
	result.capacity = g_arena.capacity;
	result.data_capacity = g_arena.data_capacity;
	result.preparation_chunks = g_arena.preparation_chunks;
	result.runtime_code_bytes = g_arena.runtime_code_bytes;
	result.runtime_data_bytes = g_arena.runtime_data_bytes;
	result.live_code_bytes = g_arena.live_code_bytes;
	result.live_data_bytes = g_arena.live_data_bytes;
	result.free_code_bytes = g_arena.code_allocator.free_bytes();
	result.free_data_bytes = g_arena.data_allocator.free_bytes();
	result.largest_free_code_bytes = g_arena.code_allocator.largest_free_bytes();
	result.largest_free_data_bytes = g_arena.data_allocator.largest_free_bytes();
	result.peak_code_bytes = g_arena.peak_code_bytes;
	result.peak_data_bytes = g_arena.peak_data_bytes;
	result.backend = g_arena.backend;
	result.expanded = g_arena.expanded;
	result.sealed = g_arena.sealed;
	return result;
}

const char* last_error() noexcept
{
	thread_local std::string copy;
	std::lock_guard lock(g_error_mutex);
	copy = g_last_error;
	return copy.c_str();
}
}
