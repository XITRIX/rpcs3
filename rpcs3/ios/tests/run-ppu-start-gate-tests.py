#!/usr/bin/env python3
"""Execute the production entry gate with Minecraft's zombie-creator state."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--ppu-source', type=Path, default=ROOT / 'rpcs3/Emu/Cell/PPUThread.cpp')
parser.add_argument('--negative-control', action='store_true')
args = parser.parse_args()
source = args.ppu_source.read_text()
start = source.index('\t\t\tif (const u32 caller_id = start_gate_caller.exchange(0))')
end = source.index('\n\t\t\tcmd_pop(), fast_call(entry_func.addr', start)
cpu = (ROOT / 'rpcs3/Emu/CPU/CPUThread.h').read_text()
flags = cpu[cpu.index('enum class cpu_flag'):cpu.index('// Test paused state')]
ppu = (ROOT / 'rpcs3/Emu/Cell/PPUThread.h').read_text()
joiner = ppu[ppu.index('enum class ppu_join_status'):ppu.index('enum ppu_thread_status')]
with tempfile.TemporaryDirectory(prefix='rpcs3-ppu-start-gate-') as directory:
    temp = Path(directory)
    (temp / 'PPUStartGateUnderTest.inc').write_text(source[start:end])
    (temp / 'CPUFlags.inc').write_text(flags + joiner)
    executable = temp / 'PPUStartGateTests'
    command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I', str(temp), '-I', str(ROOT), '-I', str(ROOT / 'rpcs3'),
               str(HERE / 'PPUStartGateTests.cpp'), '-o', str(executable)]
    if args.sanitize:
        command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(command, check=True)
    try:
        result = subprocess.run([str(executable)], timeout=5, capture_output=args.negative_control)
    except subprocess.TimeoutExpired:
        if not args.negative_control:
            raise
        print('Negative control: original production gate spins forever on the captured zombie creator')
    else:
        if args.negative_control:
            raise AssertionError('Original production gate unexpectedly completed')
        result.check_returncode()
