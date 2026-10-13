#!/usr/bin/env python3
"""Execute the production ARM64 far-jump stub through a GHC-style gateway."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--asmjit-library', type=Path, required=True)
parser.add_argument('--negative-control', action='store_true')
args = parser.parse_args()
source = (ROOT / 'rpcs3/Emu/Cell/PPUThread.cpp').read_text()
source = source[source.index('ppu_intrp_func_t gen_jump'):]
body = source[source.index('#else') + len('#else'):source.index('#endif')]
if args.negative_control:
    body = body.replace('c.mov(args[0], a64::x20);', 'c.mov(args[0], Imm(0x475));')
with tempfile.TemporaryDirectory(prefix='ppu-far-jump-') as directory:
    temp = Path(directory)
    (temp / 'production.inc').write_text(body)
    exe = temp / 'tests'
    subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++20', '-O2',
                    '-Wall', '-Wextra', '-Werror', '-I', str(temp),
                    '-isystem', str(ROOT / '3rdparty/asmjit/asmjit/src'),
                    str(HERE / 'PPUFarJumpTests.cpp'), str(args.asmjit_library.resolve()),
                    '-o', str(exe)], check=True)
    result = subprocess.run([str(exe)], capture_output=args.negative_control, timeout=10)
    if args.negative_control:
        assert result.returncode != 0, 'Wrong GHC context unexpectedly accepted'
        print('Negative control rejected: guest x0 cannot replace the thread in x20.')
    else:
        result.check_returncode()
