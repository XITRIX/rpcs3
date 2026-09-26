#!/usr/bin/env python3
"""Compile the production analyzer and native channel/wait code in host fixtures.

Only lifecycle/configuration/logging adapters are substituted. Analyzer, register
state, decoder, mailbox operations and atomic wait engine come from current source.
No game cache is required; --guest optionally checks a local 0x498 function dump.
"""
import argparse
import os
import platform
from pathlib import Path
import shlex
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--guest', type=Path)
parser.add_argument('--source', type=Path, default=ROOT / 'rpcs3/Emu/Cell/SPUCommonRecompiler.cpp')
parser.add_argument('--llvm-source', type=Path, default=ROOT / 'rpcs3/Emu/Cell/SPULLVMRecompiler.cpp')
parser.add_argument('--analyzer-only', action='store_true')
parser.add_argument('--bench', action='store_true')
args = parser.parse_args()
if platform.system() != 'Darwin' or platform.machine() not in ('arm64','aarch64'):
    raise SystemExit('ARM64 macOS host required for the native Apple wait fixture.')
out = Path(os.environ.get('SPU_MAILBOX_TEST_OUTPUT', os.environ.get('TMPDIR','/tmp')+'/rpcs3-spu-mailbox-tests'))
out.mkdir(parents=True,exist_ok=True)
s = args.source.read_text()
part = s[s.index('using reg_state_t ='):s.index('void spu_recompiler_base::dump(')]
part += s[s.index('std::array<reg_state_t, s_reg_max>& block_reg_info::evaluate_start_state'):s.index('void spu_recompiler_base::add_pattern(')]
part += s[s.index('void spu_recompiler_base::add_pattern('):s.index('extern std::string format_spu_func_info')]
(out/'SPUMailboxAnalyzer.inc').write_text(part)
s = (ROOT/'rpcs3/Emu/Cell/SPUThread.cpp').read_text()
(out/'SPUMailboxBranchTargets.inc').write_text(s[s.index('std::array<u32, 2> op_branch_targets(u32 pc, spu_opcode_t op)'):s.index('std::tuple<u32, std::array<u32, 3>, u32> op_register_targets')])
(out/'SPUMailboxTestSupport.inc').write_text((HERE/'SPUMailboxTestSupport.h').read_text())
h = (ROOT/'rpcs3/Emu/Cell/SPUThread.h').read_text()
channel = h[h.index('struct spu_channel_op_state'):h.index('struct alignas(16) spu_channel')]
channel += h[h.index('struct spu_channel_4_t'):h.index('struct spu_int_ctrl_t')]
channel += s[s.index('std::pair<u32, u32> spu_channel_4_t::pop_wait'):s.index('template <>\nvoid fmt_class_string<spu_channel>::format')]
(out/'SPUMailboxChannel.inc').write_text(channel)
t = (ROOT/'Utilities/Thread.h').read_text()
start = t.index('\ttemplate <uint Max, typename Func>')
(out/'SPUMailboxWait.inc').write_text(t[start:t.index('\t// Exit.',start)])
c = (ROOT/'rpcs3/Emu/CPU/CPUThread.h').read_text()
(out/'SPUMailboxCPUFlags.inc').write_text(c[c.index('enum class cpu_flag'):c.index('class cpu_thread')])
s = args.llvm_source.read_text()
start = s.index('auto wait_inbox = []')
end = s.index('\n\t\t\t\t};', start) + len('\n\t\t\t\t};')
(out/'SPUMailboxHelper.inc').write_text(s[start:end]+'\n')
compiler = shlex.split(os.environ.get('CXX','clang++'))
flags = ['-std=c++23','-O2','-g','-march=armv8.4-a','-Wno-invalid-constexpr',
         '-I'+str(ROOT),'-I'+str(ROOT/'rpcs3'),'-I'+str(ROOT/'3rdparty/asmjit/asmjit/src'),'-iquote',str(out)]
if platform.system() == 'Darwin':
    flags += ['-mmacosx-version-min=14.4','-Wl,-dead_strip']
else:
    flags += ['-pthread','-ffunction-sections','-fdata-sections','-Wl,--gc-sections']
flags += shlex.split(os.environ.get('SPU_MAILBOX_CXXFLAGS',''))
def run(command):
    subprocess.run([str(x) for x in command],check=True)
run([*compiler,*flags,HERE/'SPUMailboxAnalyzerTests.cpp',ROOT/'rpcs3/Crypto/sha1.cpp','-o',out/'analyzer'])
run([out/'analyzer',*([args.guest.resolve()] if args.guest else [])])
if not args.analyzer_only:
    run([*compiler,*flags,HERE/'SPUMailboxChannelTests.cpp',ROOT/'rpcs3/util/atomic.cpp','-o',out/'channel'])
    run([out/'channel',*(['--bench'] if args.bench else [])])
