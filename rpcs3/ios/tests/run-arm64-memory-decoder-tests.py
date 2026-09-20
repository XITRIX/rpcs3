#!/usr/bin/env python3
"""Test the production ARM64 fault decoder using independently assembled opcodes."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=ROOT / 'Utilities/Thread.cpp')
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
source = args.source.read_text()
start = source.index('enum mem_a64_op_t\n')
end = source.index('\nvoid put_a64_reg_value(', start)
with tempfile.TemporaryDirectory(prefix='rpcs3-arm64-memory-tests-') as directory:
    temp = Path(directory)
    (temp / 'ARM64MemoryDecoderUnderTest.inc').write_text(source[start:end])
    executable = temp / 'ARM64MemoryDecoderTests'
    command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
               '-I', str(temp), str(HERE / 'ARM64MemoryDecoderTests.cpp'), '-o', str(executable)]
    if args.sanitize:
        command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], check=True)
