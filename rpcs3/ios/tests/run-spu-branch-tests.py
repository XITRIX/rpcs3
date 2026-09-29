#!/usr/bin/env python3
"""Exercise production SPU analysis against both backend fall-through guards."""
import argparse
import os
import platform
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, default=ROOT / 'rpcs3/Emu/Cell/SPUCommonRecompiler.cpp')
parser.add_argument('--guest', type=Path, help='Optional 256 KiB local-store reconstruction of the reported demo block')
args = parser.parse_args()

with tempfile.TemporaryDirectory(prefix='spu-branch-') as directory:
    out = Path(directory)
    source = args.source.read_text()
    analyzer = source[source.index('using reg_state_t ='):source.index('void spu_recompiler_base::dump(')]
    analyzer += source[source.index('std::array<reg_state_t, s_reg_max>& block_reg_info::evaluate_start_state'):source.index('void spu_recompiler_base::add_pattern(')]
    analyzer += source[source.index('void spu_recompiler_base::add_pattern('):source.index('extern std::string format_spu_func_info')]
    (out / 'SPUMailboxAnalyzer.inc').write_text(analyzer)
    source = (ROOT / 'rpcs3/Emu/Cell/SPUThread.cpp').read_text()
    (out / 'SPUMailboxBranchTargets.inc').write_text(source[source.index('std::array<u32, 2> op_branch_targets(u32 pc, spu_opcode_t op)'):source.index('std::tuple<u32, std::array<u32, 3>, u32> op_register_targets')])
    (out / 'SPUMailboxTestSupport.inc').write_text((HERE / 'SPUMailboxTestSupport.h').read_text())
    guards = []
    for backend, filename in [('llvm', 'SPULLVMRecompiler.cpp'), ('asmjit', 'SPUASMJITRecompiler.cpp')]:
        source = (ROOT / 'rpcs3/Emu/Cell' / filename).read_text()
        guard = re.search(r'(?:else )?if \((op\.d && [^\n]+)\)\n\s*\{\n\s*// Interrupts-disable pattern', source)[1]
        lookup = 'tfound' if backend == 'llvm' else 'found'
        guards.append(f'''template <class Targets>
bool {backend}_fallthrough(const Targets& m_targets, u32 m_pos, spu_opcode_t op)
{{
    const auto {lookup} = m_targets.find(m_pos);
    if ({lookup} == m_targets.end()) return false;
    return {guard};
}}
''')
    (out / 'SPUBranchGuards.inc').write_text('\n'.join(guards))
    command = [*shlex.split(os.environ.get('CXX', 'clang++')), '-std=c++23', '-O2', '-g',
               '-Wno-invalid-constexpr', '-I' + str(ROOT), '-I' + str(ROOT / 'rpcs3'),
               '-I' + str(ROOT / '3rdparty/asmjit/asmjit/src'), '-iquote', str(out),
               *shlex.split(os.environ.get('SPU_BRANCH_CXXFLAGS', '')),
               HERE / 'SPUBranchAnalyzerTests.cpp', ROOT / 'rpcs3/Crypto/sha1.cpp',
               '-o', out / 'tests']
    command += ['-Wl,-dead_strip'] if platform.system() == 'Darwin' else ['-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections']
    subprocess.run(command, check=True)
    subprocess.run([out / 'tests', *([args.guest.resolve()] if args.guest else [])], check=True)
