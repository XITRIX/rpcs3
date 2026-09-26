#!/usr/bin/env python3
"""Run the production GPU deswizzle address functions on the host.

Clang vector extensions preserve GLSL xy/xyz swizzles. Only GLSL parameter
qualifiers and vector decrement syntax are adapted; mip/address logic is taken
directly from the shader. The oracle independently interleaves coordinate bits.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path,
                    default=ROOT / 'rpcs3/Emu/RSX/Program/GLSLSnippets/GPUDeswizzle.glsl')
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
source = args.source.read_text()


def function(name):
    start = source.index(name)
    start = source.rfind('\n', 0, start) + 1
    body = source.index('{', start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


functions = '\n'.join(function(name) for name in
                      ('init_invocation_properties(', 'get_z_index('))
functions = re.sub(r'\bin\s+', '', functions)
# GLSL supports vector ++/--; Clang's vector extension requires +=/-=.
functions = re.sub(r'(invocation\.\w+\.\w+)\s*--\s*;', r'\1 -= 1;', functions)

with tempfile.TemporaryDirectory(prefix='rpcs3-gpu-deswizzle-tests-') as directory:
    temp = Path(directory)
    (temp / 'GPUDeswizzleUnderTest.inc').write_text(functions)
    executable = temp / 'GPUDeswizzleTests'
    command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-O2',
               '-Wall', '-Wextra', '-Werror', '-I', str(temp),
               str(HERE / 'GPUDeswizzleTests.cpp'), '-o', str(executable)]
    if args.sanitize:
        command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], check=True)
