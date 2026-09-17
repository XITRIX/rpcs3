#pragma once

#include <asmjit/asmjit.h>
#include <asmjit/a64.h>
#include <cstdint>

namespace aarch64
{
// The dynamic interpreter enters with the native ABI, but STOP/interrupt helpers
// can leave through spu_escape without returning through LLVM's native frame.
// Keep a complete native callee-save frame and the PC/SP pair that ARM64 escape
// expects, independently of the interpreter's GHC instruction chain.
inline void emit_spu_interpreter_gateway(asmjit::a64::Assembler& c, std::uint32_t context_offset, std::uint64_t entry)
{
	using namespace asmjit;
	constexpr int frame_size = 176;
	c.sub(a64::sp, a64::sp, Imm(frame_size));
	c.stp(a64::x18, a64::x19, arm::Mem(a64::sp, 0));
	c.stp(a64::x20, a64::x21, arm::Mem(a64::sp, 16));
	c.stp(a64::x22, a64::x23, arm::Mem(a64::sp, 32));
	c.stp(a64::x24, a64::x25, arm::Mem(a64::sp, 48));
	c.stp(a64::x26, a64::x27, arm::Mem(a64::sp, 64));
	c.stp(a64::x28, a64::x29, arm::Mem(a64::sp, 80));
	c.str(a64::x30, arm::Mem(a64::sp, 96));
	c.stp(a64::d8, a64::d9, arm::Mem(a64::sp, 112));
	c.stp(a64::d10, a64::d11, arm::Mem(a64::sp, 128));
	c.stp(a64::d12, a64::d13, arm::Mem(a64::sp, 144));
	c.stp(a64::d14, a64::d15, arm::Mem(a64::sp, 160));

	const auto epilogue = c.newLabel();
	c.mov(a64::x14, Imm(context_offset));
	c.add(a64::x14, a64::x14, a64::x0);
	c.adr(a64::x15, epilogue);
	c.mov(a64::x16, a64::sp);
	c.stp(a64::x15, a64::x16, arm::Mem(a64::x14));
	c.mov(a64::x19, a64::x0);
	c.mov(a64::x16, Imm(entry));
	c.blr(a64::x16);

	// A normal native return preserves x19; spu_escape explicitly sets it to
	// the SPU thread. Both paths recover the frame before touching saved data.
	c.bind(epilogue);
	c.mov(a64::x14, Imm(context_offset));
	c.add(a64::x14, a64::x14, a64::x19);
	c.ldr(a64::x16, arm::Mem(a64::x14, 8));
	c.mov(a64::sp, a64::x16);
	c.ldp(a64::d8, a64::d9, arm::Mem(a64::sp, 112));
	c.ldp(a64::d10, a64::d11, arm::Mem(a64::sp, 128));
	c.ldp(a64::d12, a64::d13, arm::Mem(a64::sp, 144));
	c.ldp(a64::d14, a64::d15, arm::Mem(a64::sp, 160));
	c.ldp(a64::x18, a64::x19, arm::Mem(a64::sp, 0));
	c.ldp(a64::x20, a64::x21, arm::Mem(a64::sp, 16));
	c.ldp(a64::x22, a64::x23, arm::Mem(a64::sp, 32));
	c.ldp(a64::x24, a64::x25, arm::Mem(a64::sp, 48));
	c.ldp(a64::x26, a64::x27, arm::Mem(a64::sp, 64));
	c.ldp(a64::x28, a64::x29, arm::Mem(a64::sp, 80));
	c.ldr(a64::x30, arm::Mem(a64::sp, 96));
	c.add(a64::sp, a64::sp, Imm(frame_size));
	c.ret(a64::x30);
}
}
