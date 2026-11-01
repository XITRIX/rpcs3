#include "AOTNativeGateway.h"
#include "Emu/Cell/PPUThread.h"
#include <stdexcept>
#include <atomic>

extern "C" void rpcs3_aot_native_enter(void*, const u64*, u64*);

// Same register contract and scratch allowance as the native ARM64 gateways.
// This bridge is built into the core, never generated or patched at runtime.
asm(R"(
.text
.globl _rpcs3_aot_native_enter
.globl _rpcs3_aot_native_exit
.globl _rpcs3_aot_native_restore
.globl _rpcs3_aot_native_spu_dispatch
.globl _rpcs3_aot_native_spu_reject
.globl _rpcs3_aot_native_ppu_fallback
.p2align 2
_rpcs3_aot_native_ppu_fallback:
 mov x0, x20
 mov x1, x23
 mov x2, x24
 mov x3, x25
 b _rpcs3_aot_native_leave_ppu
.p2align 2
_rpcs3_aot_native_spu_dispatch:
 mov x3, #0
 b Lrpcs3_aot_spu_resolve
.p2align 2
_rpcs3_aot_native_spu_reject:
 mov x3, #1
Lrpcs3_aot_spu_resolve:
 mov x0, x19
 mov x1, x20
 mov x2, x21
 bl _rpcs3_aot_native_resolve_spu
 br x0
.p2align 2
_rpcs3_aot_native_enter:
 sub sp, sp, #160
 stp x19, x20, [sp, #0]
 stp x21, x22, [sp, #16]
 stp x23, x24, [sp, #32]
 stp x25, x26, [sp, #48]
 stp x27, x28, [sp, #64]
 stp x29, x30, [sp, #80]
 stp d8, d9, [sp, #96]
 stp d10, d11, [sp, #112]
 stp d12, d13, [sp, #128]
 stp d14, d15, [sp, #144]
 mov x9, x0
 ldp x19, x20, [x1, #0]
 ldp x21, x22, [x1, #16]
 ldp x23, x24, [x1, #32]
 ldr x25, [x1, #48]
 mov x26, x20
 sub sp, sp, #2, lsl #12
 mov x10, sp
 str x10, [x2, #8]
 adr x10, Lrpcs3_aot_native_exit
 str x10, [x2]
 br x9
.p2align 2
_rpcs3_aot_native_restore:
 mov sp, x0
 b _rpcs3_aot_native_exit
.p2align 2
_rpcs3_aot_native_exit:
Lrpcs3_aot_native_exit:
 add sp, sp, #2, lsl #12
 ldp x19, x20, [sp, #0]
 ldp x21, x22, [sp, #16]
 ldp x23, x24, [sp, #32]
 ldp x25, x26, [sp, #48]
 ldp x27, x28, [sp, #64]
 ldp x29, x30, [sp, #80]
 ldp d8, d9, [sp, #96]
 ldp d10, d11, [sp, #112]
 ldp d12, d13, [sp, #128]
 ldp d14, d15, [sp, #144]
 add sp, sp, #160
 ret
)");

namespace rpcs3::ios::aot
{
	namespace
	{
		struct invocation
		{
			void* cpu;
			rpcs3::hypervisor_context_t& hypervisor;
			rpcs3::hypervisor_context_t previous_hypervisor;
			invocation* previous;
			std::exception_ptr error;
			const native_spu_dispatch* dispatch;
		};
		thread_local invocation* active = nullptr;
	} // namespace

	void invoke_native(void* entry, const std::array<u64, 7>& arguments, void* cpu,
		rpcs3::hypervisor_context_t& hypervisor, const native_spu_dispatch* dispatch)
	{
		if (!entry || !cpu)
			throw std::invalid_argument("Missing native AOT entry or CPU");
		invocation call{cpu, hypervisor, hypervisor, active, {}, dispatch};
		active = &call;
		rpcs3_aot_native_enter(entry, arguments.data(), hypervisor.regs);
		active = call.previous;
		hypervisor = call.previous_hypervisor;
		if (call.error)
			std::rethrow_exception(call.error);
	}

	// Only compiler-owned RW branch slots are passed here. They are never code
	// addresses. A verification miss clears the cached target before fallback.
	void* resolve_native_spu(void* cpu, void* local_store, void** slot, bool rejected)
	{
		if (!active || active->cpu != cpu || !active->dispatch || !active->dispatch->resolve)
			throw std::logic_error("SPU native dispatch outside its registered invocation");
		if (rejected)
		{
			if (slot)
				std::atomic_ref(*slot).store(reinterpret_cast<void*>(&rpcs3_aot_native_spu_dispatch), std::memory_order_relaxed);
			escape_native_if_active(cpu);
		}
		void* target = active->dispatch->resolve(active->dispatch->owner, cpu, local_store);
		if (!target)
			escape_native_if_active(cpu);
		if (slot)
			std::atomic_ref(*slot).store(target, std::memory_order_relaxed);
		return target;
	}

	bool escape_native_if_active(void* cpu)
	{
		if (!active || active->cpu != cpu)
			return false;
		rpcs3_aot_native_restore(active->hypervisor.regs[1]);
	}

	void capture_native_exception(std::exception_ptr error)
	{
		if (!active)
			std::rethrow_exception(error);
		active->error = std::move(error);
	}

	[[noreturn]] void escape_native_exception()
	{
		if (!active)
			std::terminate();
		rpcs3_aot_native_restore(active->hypervisor.regs[1]);
	}
} // namespace rpcs3::ios::aot

extern "C" void* rpcs3_aot_native_resolve_spu(void* cpu, void* local_store, void** slot, bool rejected)
{
	return rpcs3::ios::aot::native_import<&rpcs3::ios::aot::resolve_native_spu>::call(cpu, local_store, slot, rejected);
}

// Native indirect calls already publish CIA. The three register arguments
// remain live in the GHC ABI and must be spilled before interpreter re-entry.
extern "C" void rpcs3_aot_native_leave_ppu(ppu_thread* cpu, u64 r0, u64 r1, u64 r2)
{
	cpu->gpr[0] = r0;
	cpu->gpr[1] = r1;
	cpu->gpr[2] = r2;
	if (!rpcs3::ios::aot::escape_native_if_active(cpu))
		std::terminate();
}
