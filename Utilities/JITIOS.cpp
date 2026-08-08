#include "JITIOS.h"
#include "JITAliasRegistry.h"

#if !defined(RPCS3_IOS)
#error "JITIOS.cpp is only available in the iOS frontend"
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
#include <sys/mman.h>
#include <sys/ucontext.h>
#include <unistd.h>

namespace
{
constexpr u32 breakpoint_instruction = 0xd4200000u |
	(static_cast<u32>(rpcs3::ios::jit::breakpoint_immediate) << 5);

std::mutex g_protocol_mutex;
std::mutex g_mapping_mutex;
std::mutex g_commit_mutex;
rpcs3::ios::jit::alias_registry g_mappings;
std::string g_last_error;
struct sigaction g_previous_trap_action{};

void set_error(std::string message) noexcept
{
	std::lock_guard lock(g_mapping_mutex);
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
		command != rpcs3::ios::jit::command_prepare_region)
	{
		forward_trap(signal, info, raw_context);
		return;
	}

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

u64 protocol_call(u64 command, const void* address, usz size) noexcept
{
	std::lock_guard lock(g_protocol_mutex);

	struct sigaction fallback{};
	sigemptyset(&fallback.sa_mask);
	fallback.sa_sigaction = &trap_fallback;
	fallback.sa_flags = SA_SIGINFO;

	if (::sigaction(SIGTRAP, &fallback, &g_previous_trap_action) != 0)
	{
		set_error("Unable to install the scoped JIT readiness trap handler");
		return 0;
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

bool checked_range(const void* address, usz size, uptr& begin, usz& length) noexcept
{
	if (!address || !size)
	{
		return false;
	}

	const uptr page_mask = page_size() - 1;
	const uptr raw = reinterpret_cast<uptr>(address);
	const uptr maximum = std::numeric_limits<uptr>::max();
	if (size > maximum - page_mask || raw > maximum - size - page_mask)
	{
		return false;
	}

	begin = raw & ~page_mask;
	const uptr end = (raw + size + page_mask) & ~page_mask;
	length = end - begin;
	return length != 0;
}
}

namespace rpcs3::ios::jit
{
bool is_ready() noexcept
{
	const usz length = page_size();
	// Universal JIT prepares pages from an executable mapping request. Starting
	// from PROT_NONE lets debugserver report a successful byte write while the
	// reservation remains inaccessible, which only fails later when the first
	// instruction-cache operation touches the RX view.
	void* const probe = ::mmap(nullptr, length, PROT_READ | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
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

void* reserve(usz size) noexcept
{
	if (!size)
	{
		set_error("Cannot reserve an empty executable region");
		return nullptr;
	}

	// Request the final protection up front. On iOS 26 the mapping may not become
	// usable for execution until the attached debugger prepares every page, but
	// the request must still be RX; a PROT_NONE reservation cannot be promoted by
	// Universal JIT's debugserver page writes.
	void* result = ::mmap(nullptr, size, PROT_READ | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (result == MAP_FAILED)
	{
		set_error("Unable to reserve the executable address range: " + std::string{std::strerror(errno)});
		return nullptr;
	}

	return result;
}

bool commit(void* executable, usz size) noexcept
{
	uptr begin = 0;
	usz length = 0;
	if (!checked_range(executable, size, begin, length))
	{
		set_error("Invalid executable commit range");
		return false;
	}

	std::lock_guard commit_lock(g_commit_mutex);
	{
		std::lock_guard mapping_lock(g_mapping_mutex);
		if (g_mappings.contains(begin, length)) return true;
	}

	void* const rx = reinterpret_cast<void*>(begin);
	// The debugger owns the transition from the stable reservation to RX on
	// iOS 26. An in-process mprotect(PROT_EXEC) would fail before that step.
	if (protocol_call(command_prepare_region, rx, length) != begin)
	{
		::mprotect(rx, length, PROT_NONE);
		set_error("The debugger did not prepare the requested executable range");
		return false;
	}

	vm_address_t alias = 0;
	vm_prot_t current_protection = VM_PROT_NONE;
	vm_prot_t maximum_protection = VM_PROT_NONE;
	const kern_return_t remap_result = ::vm_remap(
		mach_task_self(),
		&alias,
		static_cast<vm_size_t>(length),
		0,
		VM_FLAGS_ANYWHERE,
		mach_task_self(),
		static_cast<vm_address_t>(begin),
		false,
		&current_protection,
		&maximum_protection,
		VM_INHERIT_SHARE);

	if (remap_result != KERN_SUCCESS)
	{
		::mprotect(rx, length, PROT_NONE);
		set_error("mach_vm_remap failed while creating the writable JIT alias");
		return false;
	}

	if (::vm_protect(mach_task_self(), alias, static_cast<vm_size_t>(length), false, VM_PROT_READ | VM_PROT_WRITE) != KERN_SUCCESS)
	{
		::vm_deallocate(mach_task_self(), alias, static_cast<vm_size_t>(length));
		::mprotect(rx, length, PROT_NONE);
		set_error("mach_vm_protect failed for the writable JIT alias");
		return false;
	}

	bool registered = false;
	{
		std::lock_guard mapping_lock(g_mapping_mutex);
		registered = g_mappings.insert(begin, static_cast<uptr>(alias), length);
	}
	if (!registered)
	{
		::vm_deallocate(mach_task_self(), alias, static_cast<vm_size_t>(length));
		::mprotect(rx, length, PROT_NONE);
		set_error("Unable to register the writable JIT alias");
		return false;
	}

	return true;
}

void* writable(const void* executable, usz size) noexcept
{
	if (!executable || !size)
	{
		return nullptr;
	}

	const uptr address = reinterpret_cast<uptr>(executable);
	if (address > std::numeric_limits<uptr>::max() - size)
	{
		return nullptr;
	}

	std::lock_guard lock(g_mapping_mutex);
	return g_mappings.translate(address, size);
}

void flush(const void* executable, usz size) noexcept
{
	if (!executable || !size)
	{
		return;
	}

	// Clean through the address that received the writes, then invalidate the
	// instruction view that will actually be executed.
	void* const alias = writable(executable, size);
	::sys_dcache_flush(alias ? alias : const_cast<void*>(executable), size);
	::sys_icache_invalidate(const_cast<void*>(executable), size);
}

void release(void* executable, usz size) noexcept
{
	if (!executable || !size)
	{
		return;
	}

	const uptr begin = reinterpret_cast<uptr>(executable);
	std::lock_guard commit_lock(g_commit_mutex);
	std::lock_guard mapping_lock(g_mapping_mutex);

	g_mappings.remove_contained(begin, size, [](const alias_mapping& item)
	{
		::vm_deallocate(mach_task_self(), static_cast<vm_address_t>(item.writable), static_cast<vm_size_t>(item.size));
	});

	::munmap(executable, size);
}

const char* last_error() noexcept
{
	thread_local std::string copy;
	std::lock_guard lock(g_mapping_mutex);
	copy = g_last_error;
	return copy.c_str();
}
}
