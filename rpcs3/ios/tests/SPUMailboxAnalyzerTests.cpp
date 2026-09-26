#include "stdafx.h"
#include "Emu/Cell/SPURecompiler.h"
#include "Emu/Cell/SPUDisAsm.h"
#include "Emu/system_config_types.h"
#include "Crypto/sha1.h"
#include "util/asm.hpp"
#include "Emu/Cell/Modules/cellSync.h"
#include <iostream>
#include <fstream>
#include <set>
#include <unordered_set>
const extern spu_decoder<spu_itype> g_spu_itype{};
const extern spu_decoder<spu_iname> g_spu_iname{};
const extern spu_decoder<spu_iflag> g_spu_iflag{};
constexpr u32 s_reg_max = spu_recompiler_base::s_reg_max;
struct
{
	struct
	{
		spu_block_size_type spu_block_size = spu_block_size_type::safe;
		bool spu_debug = false;
		bool spu_accurate_reservations = true;
	} core;
} fixture_cfg;
struct fixture_types
{
	template <class T>
	T& get()
	{
		static T obj;
		return obj;
	}
} fixture_types_instance;
constexpr auto fixture_fxo = &fixture_types_instance;
struct sink
{
	bool enabled = false;
	template <class... T>
	void operator()(T&&...) const
	{
	}
	constexpr explicit operator bool() const
	{
		return enabled;
	}
	const sink& operator()() const
	{
		return *this;
	}
};
const struct
{
	sink notice, warning, error, success, trace, always, fatal, todo;
} fixture_log;
#define g_cfg fixture_cfg
#define g_fxo fixture_fxo
#define spu_log fixture_log
#include "SPUMailboxTestSupport.inc"
#include "SPUMailboxBranchTargets.inc"
#include "SPUMailboxAnalyzer.inc"
spu_recompiler_base::spu_recompiler_base() {}
spu_recompiler_base::~spu_recompiler_base() {}
void spu_recompiler_base::dump(const spu_program&, std::string&, u32, u32) {}
struct analyzer : spu_recompiler_base
{
	using spu_recompiler_base::inst_attr;
	using spu_recompiler_base::m_inst_attrs;
	using spu_recompiler_base::m_patterns;
	void init() override {}
	spu_function_t compile(spu_program&&) override
	{
		return nullptr;
	}
};

static u32 rch(unsigned reg, unsigned ch = SPU_RdInMbox)
{
	return 0x01e00000 | ch << 7 | reg;
}
static u32 ceq(unsigned dst, unsigned src, int imm)
{
	return 0x7c000000 | (static_cast<u32>(imm) & 1023) << 14 | src << 7 | dst;
}
static u32 branch(unsigned reg, int words, bool zero)
{
	return (zero ? 0x20000000 : 0x21000000) | (static_cast<u32>(words) & 65535) << 7 | reg;
}
static u32 copy(unsigned dst, unsigned src)
{
	return 0x04000000 | src << 7 | dst;
} // ORI 0
static unsigned cases = 0;
static void test(const std::vector<u32>& code, bool expected, const char* label, u32 base = 0x100, u32 read_offset = 0)
{
	auto a = std::make_unique<analyzer>();
	std::vector<be_t<u32>> ls(65536);
	for (usz i = 0; i < code.size(); i++)
		ls[base / 4 + i] = code[i];
	const auto program = a->analyse(ls.data(), base);
	const bool detected = read_offset / 4 < a->m_inst_attrs.size() && a->m_inst_attrs[read_offset / 4] == analyzer::inst_attr::rchcnt_loop;
	if (detected != expected)
	{
		std::cerr << "Analyzer mismatch: " << label << " expected=" << expected << " actual=" << detected << " words=" << program.data.size() << " attributes=" << a->m_inst_attrs.size() << '\n';
		std::exit(1);
	}
	// Recognition may annotate an instruction, but must not rewrite guest bytes.
	for (usz i = 0; i < program.data.size(); i++)
		if (program.data[i])
			ensure(program.data[i] == std::bit_cast<u32>(ls[program.lower_bound / 4 + i]));
	cases++;
}
int main(int argc, char** argv)
{
	for (unsigned count = 2; count < 18; count++)
		for (unsigned cmp = 2; cmp < 18; cmp++)
			for (int imm : {0, 1})
				for (bool zero : {false, true})
				{
					const bool empty_loops = zero == (imm == 1);
					test({rch(count), ceq(cmp, count, imm), branch(cmp, -2, zero), 0x01a00e88, 0x35000000}, empty_loops, "CEQI branch polarity/aliases");
				}
	test({rch(3), branch(3, -1, true), 0x01a00e88, 0x35000000}, true, "direct count");
	test({rch(3), ceq(2, 3, 0), branch(3, -2, true), 0x01a00e88, 0x35000000}, true, "branch uses count after CEQI zero");
	test({rch(3), ceq(2, 3, 1), branch(3, -2, true), 0x01a00e88, 0x35000000}, true, "branch uses count after CEQI one");
	test({rch(3), ceq(2, 3, 0), branch(3, -2, false), 0x01a00e88, 0x35000000}, false, "nonempty count loop after CEQI");
	test({rch(3), copy(9, 3), ceq(2, 9, 1), copy(10, 2), branch(10, -4, true), 0x01a00e88, 0x35000000}, true, "copied count and comparison");
	test({rch(3), ceq(2, 3, 2), branch(2, -2, true), 0x01a00e88, 0x35000000}, false, "unsupported comparison immediate");
	test({rch(3), ceq(2, 3, 1), 0x14000102, branch(2, -3, true), 0x01a00e88, 0x35000000}, false, "comparison masked to zero"); // ANDI r2,r2,0
	test({rch(3), 0x1c004183, ceq(2, 3, 1), branch(2, -3, true), 0x01a00e88, 0x35000000}, false, "modified channel count");    // AI r3,r3,1
	test({rch(3), 0x1c00450a, ceq(2, 3, 1), branch(2, -3, true), 0x01a00e88, 0x35000000}, false, "loop-carried counter");      // AI r10,r10,1
	test({rch(3), 0x24004083, ceq(2, 3, 1), branch(2, -3, true), 0x01a00e88, 0x35000000}, false, "guest store in loop");
	test({rch(3), rch(4, SPU_RdSigNotify1), ceq(2, 3, 1), branch(2, -3, true), 0x01a00e88, 0x35000000}, false, "second channel read in loop");
	test({rch(3), ceq(2, 3, 1), branch(2, -2, true), 0x01a00e88, rch(5), ceq(6, 5, 1), branch(6, -2, true), 0x01a00e89, 0x35000000}, true, "consecutive independent loops");
	test({rch(3), ceq(2, 3, 1), branch(2, -2, true), 0x01a00e88, rch(5), ceq(6, 5, 1), branch(6, -2, true), 0x01a00e89, 0x35000000}, false, "second loop conservatively falls back", 0x100, 16);
	test({rch(3), 0x40200000, 0x00200000, ceq(2, 3, 1), branch(2, -4, true), 0x01a00e88, 0x35000000}, true, "NOP and LNOP in loop");
	std::vector<u32> long_loop{rch(3)};
	long_loop.insert(long_loop.end(), 8, 0x40200000);
	long_loop.insert(long_loop.end(), {ceq(2, 3, 1), branch(2, -10, true), 0x01a00e88, 0x35000000});
	test(long_loop, false, "long loop exceeds bounded shape");
	test({rch(3, SPU_RdSigNotify1), ceq(2, 3, 1), branch(2, -2, true), 0x01a00183, 0x35000000}, false, "new comparison waits remain scoped to inbox");
	test({rch(3, SPU_RdSigNotify1), branch(3, -1, true), 0x01a00183, 0x35000000}, true, "existing direct signal count detection");
	// The tested hot sequence, preceded by its ordinary stack prologue.
	test({0x24004080, 0x24ff8081, 0x1cf80081, rch(3), ceq(2, 3, 1), branch(2, -2, true), 0x427d7d06, 0x01a00e88, 0x35000000}, true, "stack prologue and mailbox read", 0x498, 12);
	if (argc == 2)
	{
		std::ifstream input(argv[1], std::ios::binary);
		ensure(input.good());
		std::vector<unsigned char> raw((std::istreambuf_iterator<char>(input)), {});
		ensure(raw.size() % 4 == 0);
		std::vector<u32> code;
		for (usz i = 0; i < raw.size(); i += 4)
			code.push_back(u32(raw[i]) << 24 | u32(raw[i + 1]) << 16 | u32(raw[i + 2]) << 8 | raw[i + 3]);
		test(code, true, "complete recovered DiRT 2 function", 0x498, 12);
	}
	std::cout << "Production SPU analyzer: " << cases << " recognition/guard cases passed\n";
}
