#include "Utilities/StrFmt.h"
#include "util/simd.hpp"
#include "Emu/Cell/SPUOpcodes.h"

#include <cfenv>
#include <cstdio>
#include <cstdlib>
#include <random>

#undef FORCE_INLINE
#include "Emu/CPU/sse2neon.h"

// Only the process-global utility probes need stubs; all vector helpers,
// opcode fields and instruction bodies below are production code.
namespace utils
{
bool has_avx() { return false; }
unsigned long long _get_main_tid() { return 1; }
}

enum class spu_exec_bit { use_dfma };
struct spu_thread { v128 gpr[128]{}; };

#include "SPUStaticFloatingPointUnderTest.h"

// The independent scalar oracle must honor the SPU thread's rounding mode.
#pragma STDC FENV_ACCESS ON

namespace
{
enum operation { add, subtract, negative_subtract };
constexpr const char* names[]{"FMA", "FMS", "FNMS"};
using instruction = bool (*)(spu_thread&, spu_opcode_t);
constexpr instruction instructions[]{FMA<>, FMS<>, FNMS<>};
u64 checks = 0;

[[noreturn]] void fail(operation kind, u32 lane, u32 actual, u32 expected)
{
	std::fprintf(stderr, "%s lane %u: got %08x, expected %08x\n", names[kind], lane, actual, expected);
	std::exit(EXIT_FAILURE);
}

__attribute__((noinline)) float reference(operation kind, float a, float b, float c)
{
	if (kind == subtract) return std::fma(a, b, -c);
	if (kind == negative_subtract) return std::fma(-a, b, c);
	return std::fma(a, b, c);
}

void check(operation kind, v128 a, v128 b, v128 c, u32 destination)
{
	spu_thread spu;
	spu.gpr[1] = a;
	spu.gpr[2] = b;
	spu.gpr[3] = c;
	spu_opcode_t op{};
	op.ra = 1;
	op.rb = 2;
	op.rc = 3;
	op.rt4 = destination;
	if (!instructions[kind](spu, op)) std::abort();
	for (u32 lane = 0; lane < 4; ++lane)
	{
		const auto expected = std::bit_cast<u32>(reference(kind, a._f[lane], b._f[lane], c._f[lane]));
		const auto actual = spu.gpr[destination]._u32[lane];
		if (actual != expected) fail(kind, lane, actual, expected);
		++checks;
	}
	for (u32 reg = 1; reg <= 3; ++reg)
	{
		const v128 before[]{a, b, c};
		if (reg != destination && spu.gpr[reg] != before[reg - 1]) std::abort();
	}
}
}

int main()
{
	std::fesetround(FE_TOWARDZERO);
	gv_set_zeroing_denormals();

	// Exact cancellation regression: 4097^2 = 16785409. A rounded product
	// becomes 16785408 and incorrectly wipes out the entire result.
	for (u32 kind = 0; kind < 3; ++kind)
	{
		for (u32 destination : {1u, 2u, 3u, 4u})
		{
			check(operation(kind), v128::fromf32p(4097.f), v128::fromf32p(4097.f),
				v128::fromf32p(kind == add ? -16785408.f : 16785408.f), destination);
		}
	}

	// Unpack biased integer coordinates using a non-power-of-two scale.
	// Premature product rounding turns a smooth ramp into coarse steps.
	for (float scale : {0.001f, -0.001f, 0.1f, -0.1f})
	{
		for (u32 value = 0; value < 65536; value += 4)
		{
			v128 packed;
			for (u32 lane = 0; lane < 4; ++lane) packed._f[lane] = 8388608.f + (value + lane);
			for (u32 kind = 0; kind < 3; ++kind)
			{
				const float bias = 8388608.f * scale;
				check(operation(kind), packed, v128::fromf32p(scale),
					v128::fromf32p(kind == add ? -bias : bias), 1 + (value / 4) % 4);
			}
		}
	}

	// Finite mixed-sign vectors, cancellation, signed zeros, and every
	// destination/source alias. No game, renderer, or JIT arena is involved.
	std::mt19937 random(0x535055);
	for (u32 iteration = 0; iteration < 20000; ++iteration)
	{
		v128 inputs[3];
		for (auto& input : inputs)
		{
			for (u32 lane = 0; lane < 4; ++lane)
			{
				const u32 bits = random();
				input._u32[lane] = (bits & 0x807fffff) | ((80 + random() % 81) << 23);
				if (iteration % 11 == 0) input._u32[lane] &= 0x80000000;
			}
		}
		for (u32 kind = 0; kind < 3; ++kind)
			check(operation(kind), inputs[0], inputs[1], inputs[2], 1 + iteration % 4);
	}

	// FMS must retain the sign/payload of an extended addend interpreted by
	// the host as a quiet NaN, just as the existing LLVM negate_addend does.
	for (u32 bits : {0x7fc12345u, 0xffc12345u})
	{
		spu_thread spu;
		spu.gpr[1] = v128::fromf32p(1.f);
		spu.gpr[2] = v128::from32p(bits);
		spu_opcode_t op{};
		op.ra = 1; op.rb = 1; op.rc = 2; op.rt4 = 2;
		FMS<>(spu, op);
		if (spu.gpr[2]._u32[0] != bits) fail(subtract, 0, spu.gpr[2]._u32[0], bits);
	}
	std::printf("%llu SPU multiply-add lane checks passed, including coordinate ramps and register aliases\n", checks);
}
