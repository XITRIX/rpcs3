#pragma once

// Ledger-tagged backing for virgin JIT reservations. Request accounting before
// mapping; never transfer ownership to another task or make pages volatile.
// Accounting refusal is not a JIT failure.
#include <dlfcn.h>
#include <initializer_list>
#include <mach/mach.h>
#include <mach/vm_statistics.h>

namespace rpcs3::ios::jit
{
using memory_ownership_function = kern_return_t (*)(mach_port_t, mach_port_t, int, int);

struct footprint_sample
{
	uint64_t bytes = 0;
	bool valid = false;
};

inline footprint_sample sample_footprint() noexcept
{
	task_vm_info_data_t info{};
	mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
	const auto kr = ::task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count);
	return {info.phys_footprint, kr == KERN_SUCCESS && count >= TASK_VM_INFO_REV1_COUNT};
}

struct footprint_request
{
	memory_object_size_t requested = 0;
	memory_object_size_t covered = 0;
	kern_return_t entry_result = KERN_FAILURE;
	kern_return_t ownership_result = KERN_NOT_SUPPORTED;
	footprint_sample before;
	footprint_sample after;
	bool shared_entry = false;
	bool called = false;
};

inline memory_ownership_function resolve_memory_ownership() noexcept
{
	// Optional lookup keeps older systems loadable when this private API is absent.
	static const auto function = reinterpret_cast<memory_ownership_function>(
		::dlsym(RTLD_DEFAULT, "mach_memory_entry_ownership"));
	return function;
}

inline void request_no_footprint(mach_port_t entry, footprint_request& result,
	memory_ownership_function function) noexcept
{
	result.before = sample_footprint();
	if (function)
	{
		result.called = true;
		// TASK_NULL means keep the current owner, not transfer to the system.
		// Use SDK constants: VM_LEDGER_TAG_DEFAULT is 1, not Madeira's fallback 0.
		result.ownership_result = function(entry, MACH_PORT_NULL,
			VM_LEDGER_TAG_DEFAULT, VM_LEDGER_FLAG_NO_FOOTPRINT);
	}
	result.after = sample_footprint();
}

// Create virgin backing, tag it, request ownership, THEN map it. This follows
// the allocation order of the older MeloNX design, without copying its code.
// Only replace a reservation owned by the caller, never live generated code.
inline bool map_ledger_backing(void* address, memory_object_size_t size, vm_prot_t protection,
    footprint_request& result, memory_ownership_function function = resolve_memory_ownership()) noexcept
{
    result = {};
    result.requested = result.covered = size;
    if (!address || !size || size % vm_page_size || (reinterpret_cast<uintptr_t>(address) % vm_page_size))
    {
        result.entry_result = KERN_INVALID_ARGUMENT;
        return false;
    }
    mach_port_t entry = MACH_PORT_NULL;
    result.entry_result = ::mach_make_memory_entry_64(mach_task_self(), &result.covered, 0,
        MAP_MEM_NAMED_CREATE | MAP_MEM_LEDGER_TAGGED | VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE,
        &entry, MACH_PORT_NULL);
    if (result.entry_result != KERN_SUCCESS || entry == MACH_PORT_NULL || result.covered != size)
    {
        if (entry != MACH_PORT_NULL) ::mach_port_deallocate(mach_task_self(), entry);
        return false;
    }
    request_no_footprint(entry, result, function);
    vm_address_t mapped = reinterpret_cast<vm_address_t>(address);
    const auto kr = ::vm_map(mach_task_self(), &mapped, size, 0,
        address ? VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE : VM_FLAGS_ANYWHERE,
        entry, 0, false, protection, VM_PROT_ALL, VM_INHERIT_NONE);
    ::mach_port_deallocate(mach_task_self(), entry);
    return kr == KERN_SUCCESS && (!address || mapped == reinterpret_cast<vm_address_t>(address));
}

// Populate after the ownership request. Reports charged growth, rather than
// pretending a request over an untouched reservation saved its entire capacity.
inline footprint_request probe_named_footprint(
    memory_ownership_function function = resolve_memory_ownership()) noexcept
{
    footprint_request result;
    constexpr vm_size_t bytes = 4 * 1024 * 1024;
    vm_address_t address = 0;
    if (::vm_allocate(mach_task_self(), &address, bytes, VM_FLAGS_ANYWHERE) != KERN_SUCCESS) return result;
    if (map_ledger_backing(reinterpret_cast<void*>(address), bytes, VM_PROT_READ | VM_PROT_WRITE, result, function))
    {
        result.before = sample_footprint();
        auto* memory = reinterpret_cast<volatile unsigned char*>(address);
        for (vm_size_t i = 0; i < bytes; i += vm_page_size) memory[i] = 0x5a;
        result.after = sample_footprint();
    }
    ::vm_deallocate(mach_task_self(), address, bytes);
    return result;
}
}
