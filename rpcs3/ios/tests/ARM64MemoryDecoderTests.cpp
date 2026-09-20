#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>

using u32 = std::uint32_t;
#include "ARM64MemoryDecoderUnderTest.inc"

struct test_case
{
	u32 instruction;
	mem_a64_op_t operation;
	u32 memory_size;
	u32 register_size;
	u32 register_index;
	bool is_signed;
};

int main()
{
	// Encodings independently assembled with LLVM MC. The first instruction is
	// the live Prototype 2 NPC write: str w9, [x22, x8], bytes c9 6a 28 b8.
	constexpr test_case cases[] =
	{
		{0xb8286ac9, A64_STORE, 4, 4, 9, false},
		{0xb8284ac9, A64_STORE, 4, 4, 9, false}, // UXTW
		{0xb828dac9, A64_STORE, 4, 4, 9, false}, // SXTW #2
		{0xb8287ac9, A64_STORE, 4, 4, 9, false}, // LSL #2
		{0xb828fac9, A64_STORE, 4, 4, 9, false}, // SXTX #2
		{0x38286ac9, A64_STORE, 1, 1, 9, false}, // STRB
		{0x78286ac9, A64_STORE, 2, 2, 9, false}, // STRH
		{0xf8286ac9, A64_STORE, 8, 8, 9, false}, // STR Xt
		{0x38686ac9, A64_LOAD, 1, 4, 9, false},  // LDRB
		{0x78686ac9, A64_LOAD, 2, 4, 9, false},  // LDRH
		{0xb8686ac9, A64_LOAD, 4, 4, 9, false}, // LDR Wt
		{0xf8686ac9, A64_LOAD, 8, 8, 9, false}, // LDR Xt
		{0x38e86ac9, A64_LOAD, 1, 4, 9, true},  // LDRSB Wt
		{0x38a86ac9, A64_LOAD, 1, 8, 9, true},  // LDRSB Xt
		{0x78e86ac9, A64_LOAD, 2, 4, 9, true},  // LDRSH Wt
		{0x78a86ac9, A64_LOAD, 2, 8, 9, true},  // LDRSH Xt
		{0xb8a86ac9, A64_LOAD, 4, 8, 9, true},  // LDRSW Xt
		{0xb8286bff, A64_STORE, 4, 4, 31, false}, // STR WZR, [SP, X8]
		{0xb90006c9, A64_STORE, 4, 4, 9, false}, // STR Wt, unsigned immediate
		{0xb81fc2c9, A64_STORE, 4, 4, 9, false}, // STUR Wt, negative immediate
		{0xf94006c9, A64_LOAD, 8, 8, 9, false}, // LDR Xt, unsigned immediate
		{0x38dff2c9, A64_LOAD, 1, 4, 9, true},  // LDURSB Wt
	};
	for (const auto& test : cases)
	{
		const auto actual = decode_a64_mem_inst(test.instruction);
		if (actual.op != test.operation || actual.mem_size != test.memory_size ||
			actual.reg_size != test.register_size || actual.reg_num != test.register_index ||
			actual.reg_signed != test.is_signed)
		{
			std::fprintf(stderr, "Incorrect ARM64 memory decode: 0x%08x\n", test.instruction);
			return 1;
		}
	}

	// These require additional side effects, different register banks, or are
	// unallocated. Never accept them as a simple scalar MMIO transfer.
	constexpr u32 rejected[] =
	{
		0xb8004ec9, // STR pre-index (base writeback)
		0xb80046c9, // STR post-index (base writeback)
		0x29002ac9, // STP
		0xbc286ac9, // STR SIMD/FP
		0xf8a86ac0, // PRFM
		0xb82982ca, // SWP
		0x889ffec9, // STLR
		0x88087ec9, // STXR
		0xb8e86ac9, // Unallocated signed 32-bit load to Wt
		0xf8e86ac9, // Unallocated opc=3, size=3
		0xb8280ac9, 0xb8282ac9, 0xb8288ac9, 0xb828aac9, // Invalid extend options
	};
	for (u32 instruction : rejected)
	{
		assert(decode_a64_mem_inst(instruction).op == A64_INVALID);
	}
	std::puts("ARM64 memory decoder tests passed");
}
