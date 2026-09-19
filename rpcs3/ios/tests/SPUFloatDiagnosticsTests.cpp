#include "Utilities/StrFmt.h"
#include "util/simd.hpp"
#include "Emu/Cell/SPUOpcodes.h"
#include "Emu/Cell/SPUInterpreterDiagnostics.h"

#include <cfenv>
#include <cstdio>
#include <cstdlib>

#undef FORCE_INLINE
#include "Emu/CPU/sse2neon.h"

namespace utils
{
bool has_avx() { return false; }
unsigned long long _get_main_tid() { return 1; }
}

enum class spu_exec_bit { use_dfma };
enum class spu_itype { FA, FS, FM, FI, FMA, FMS, FNMS, FCGT, FCMGT, FCEQ, FCMEQ, CFLTS, CFLTU, UNK };
struct
{
	struct { v128 operator[](int n) const { return v128::from32p(n > 127 ? 0x7f800000u : u32(n + 127) << 23); } } scale;
} g_spu_imm;
struct spu_thread { std::array<v128, 128> gpr{}; u32 pc = 0x1000; };
using spu_intrp_func_t = bool(*)(spu_thread&, spu_opcode_t);
struct spu_interpreter { static bool diagnose(spu_thread&, spu_opcode_t, spu_intrp_func_t); };
struct { spu_itype type; spu_itype decode(u32) const { return type; } } g_spu_itype;
struct { const char* decode(u32) const { return "test"; } } g_spu_iname;
struct
{
	u32 reports = 0;
	u32 boundaries = 0;
	template <typename... T> void notice(const char*, T&&...) {}
	template <typename... T> void warning(const char* format, T&&...)
	{
		if (std::string_view(format).starts_with("Static SPU refinement boundary:")) ++boundaries;
		else ++reports;
	}
} spu_log;

#include "SPUFloatDiagnosticsUnderTest.h"

void require(bool value)
{
	if (!value) std::abort();
}

int main()
{
	using namespace spu_float_diagnostics;
	std::fesetround(FE_TOWARDZERO);
	gv_set_zeroing_denormals();
	// Exact reference cases include finite cancellation and extended operands.
	require(reference(operation::fma, 0x45800800, 0x45800800, 0xcb801000) == 0x3f800000);
	require(reference(operation::fms, 0x7fffffff, 0x3f800000, 0) == 0x7f7fffff);
	require(reference(operation::fnms, 0x7fffffff, 0x3f800000, 0) == 0xff7fffff);
	require(reference(operation::fi, 0x0007ffff, 0x3f800001, 0) == 0x3f7ff800);
	require(reference(operation::fcgt, 0xbf800000, 0xc0000000, 0) == 0xffffffff);
	require(reference(operation::fcgt, 0x80000000, 0, 0) == 0);
	require(reference(operation::fcmgt, 0x7fffffff, 0x7fc00000, 0) == 0xffffffff);
	require(reference(operation::fceq, 0x7fffffff, 0x7fffffff, 0) == 0xffffffff);
	require(reference(operation::fcmeq, 0xffffffff, 0x7fffffff, 0) == 0xffffffff);
	require(!significant(0, 0x80000000));
	require(!significant(0x3f800001, 0x3f800000));
	require(significant(0x40000000, 0x3f800000));
	require(significant(0x7f800000, 0x7f7fffff));
	require(reference(operation::cflts, 0x7ffff800, 0, 0) == 0x7fffffffu);
	require(reference(operation::cflts, 0xfffff800, 0, 0) == 0x80000000u);
	require(reference(operation::cfltu, 0x7ffff800, 0, 0) == 0xffffffffu);
	require(reference(operation::cfltu, 0xfffff800, 0, 0) == 0);
	require(reference(operation::cflts, 0x3fc00000, 0, 0, 172) == 3);
	require(reference(operation::cfltu, 0x3fc00000, 0, 0, 174) == 0);
	require(reference(operation::cflts, 0, 0, 0, 0) == 0x7fffffffu);
	require(refinement_boundary(0, 0x7fb50160) == 0);
	require(refinement_boundary(1, 0x7fb50160) == 1);
	require(refinement_boundary(0xbf800000, 0x3f7ffdf4) == 2);
	require(refinement_boundary(0xbf800000, 0xbf7ffbe0) == 0);
	require(refinement_boundary(0x7e800000, 0x007ffbe0) == 4);

	spu_thread spu;
	spu_opcode_t op{};
	op.ra = 1; op.rb = 2; op.rc = 3; op.rt4 = 1;
	spu.gpr[1] = v128::from32p(0x45800800);
	spu.gpr[2] = spu.gpr[1];
	spu.gpr[3] = v128::from32p(0xcb801000);
	g_spu_itype.type = spu_itype::FMA;
	const auto original = spu;
	auto direct = original;
	FMA<>(direct, op);
	require(spu_interpreter::diagnose(spu, op, FMA<>));
	require(spu.gpr == direct.gpr && spu.pc == original.pc && spu_log.reports == 0);

	// Detect the old cancellation error without substituting the reference.
	const auto broken = +[](spu_thread& cpu, spu_opcode_t inst)
	{
		cpu.gpr[inst.rt4] = v128{};
		return true;
	};
	spu = original;
	require(spu_interpreter::diagnose(spu, op, broken));
	require(spu.gpr[1] == v128{} && spu_log.reports == 1);
	spu = original;
	spu_interpreter::diagnose(spu, op, broken);
	require(spu_log.reports == 1);

	// A real extended-value difference is reported, retaining native results.
	spu = original;
	spu.gpr[1] = v128::from32p(0x7fffffff);
	spu.gpr[2] = v128::from32p(0x3f800000);
	spu.gpr[3] = v128{};
	spu.pc += 4;
	direct = spu;
	FMS<>(direct, op);
	g_spu_itype.type = spu_itype::FMS;
	spu_interpreter::diagnose(spu, op, FMS<>);
	require(spu.gpr == direct.gpr && spu_log.reports == 2);

	g_spu_itype.type = spu_itype::FCMGT;
	op.rt = 4;
	spu.gpr[op.ra] = v128::from32p(0x7fffffff);
	spu.gpr[op.rb] = v128::from32p(0x7fc00000);
	direct = spu;
	FCMGT<>(direct, op);
	spu_interpreter::diagnose(spu, op, FCMGT<>);
	require(spu.gpr == direct.gpr && spu_log.reports == 3);

	// Non-floating control flow is invoked exactly once with its return intact.
	g_spu_itype.type = spu_itype::UNK;
	const u32 old_pc = spu.pc;
	require(!spu_interpreter::diagnose(spu, op, +[](spu_thread& cpu, spu_opcode_t)
	{
		cpu.pc += 8;
		return false;
	}));
	require(spu.pc == old_pc + 8 && spu_log.reports == 3);

	// Conversions compare integer results exactly, including a one-bit error.
	g_spu_itype.type = spu_itype::CFLTS;
	op = {}; op.ra = 1; op.rt = 1; op.i8 = 173;
	spu.pc = 0x1800;
	spu.gpr[1] = v128::from32p(0x3f800000);
	spu_interpreter::diagnose(spu, op, +[](spu_thread& cpu, spu_opcode_t inst)
	{
		cpu.gpr[inst.rt] = v128::from32p(2);
		return true;
	});
	require(spu.gpr[1] == v128::from32p(2) && spu_log.reports == 4);
	for (auto fn : {CFLTS<>, CFLTU<>})
	{
		g_spu_itype.type = fn == CFLTS<> ? spu_itype::CFLTS : spu_itype::CFLTU;
		for (u32 scale : {0, 142, 172, 173, 174, 255})
		{
			op.i8 = scale;
			for (u32 bits : {0u, 0x3fc00000u, 0xbfc00000u, 0x4f000000u, 0x4f800000u, 0x7ffff800u, 0xfffff800u})
			{
				spu.gpr[1] = v128::from32p(bits);
				direct = spu;
				fn(direct, op);
				spu_interpreter::diagnose(spu, op, fn);
				require(spu.gpr == direct.gpr);
			}
		}
	}

	g_spu_itype.type = spu_itype::FMA;
	op.ra = 1; op.rb = 2; op.rc = 3; op.rt4 = 1;
	for (u32 i = 0; i < 100; ++i)
	{
		spu = original;
		spu.pc += 16 + i * 4;
		spu_interpreter::diagnose(spu, op, broken);
	}
	// Finite/conversion cap is independent of the two earlier extended reports.
	require(spu_log.reports == 34);

	// Address relocation does not repeatedly spend the extended-value budget.
	g_spu_itype.type = spu_itype::FI;
	op = {}; op.ra = 1; op.rb = 2; op.rt = 3;
	for (u32 i = 0; i < 100; ++i)
	{
		spu.pc = 0x2000 + i * 0x400;
		spu.gpr[1] = v128{};
		spu.gpr[2] = v128::from32p(0x7ffffbe0);
		spu_interpreter::diagnose(spu, op, FI<>);
	}
	require(spu_log.reports == 35 && spu_log.boundaries == 0);
	for (u32 i = 0; i < 100; ++i)
	{
		spu.pc = 0x3000 + i * 0x400;
		op.rt = i % 128;
		spu.gpr[1] = v128::from32p(0xbf800000);
		spu.gpr[2] = v128::from32p(0x3f7ffdf4);
		direct = spu;
		FI<>(direct, op);
		spu_interpreter::diagnose(spu, op, FI<>);
		require(spu.gpr == direct.gpr);
	}
	require(spu_log.boundaries == 16);
	for (u32 i = 0; i < 100; ++i)
	{
		op.rt = i % 128;
		spu.gpr[1] = v128{};
		spu.gpr[2] = v128::from32p(0x7ffffbe0);
		spu_interpreter::diagnose(spu, op, FI<>);
	}
	require(spu_log.reports == 48 && spu_log.boundaries == 16);
	std::puts("Live SPU probe preserves execution, checks integer conversions, separates finite/extended budgets and bounds refinement reports");
}
