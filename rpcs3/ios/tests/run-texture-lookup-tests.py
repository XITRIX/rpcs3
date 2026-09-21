#!/usr/bin/env python3
"""Execute production candidate collection/selection with instrumented texture storage.

Only the GPU view and backing storage are modeled. The lookup, validation order,
exact-hit selection, swizzle rejection and merge filtering come from texture_cache.h.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=ROOT / 'rpcs3/Emu/RSX/Common/texture_cache.h')
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
source = args.source.read_text()
start = source.rindex('\n\t\ttemplate <', 0, source.index('find_texture_from_range(const'))
end = source.index('\n\t\ttemplate <', source.index('find_texture_from_range(const'))
collection = source[start:end]
start = source.index('\t\t\t// Check shader_read storage.')
end = source.index('\n\t\t\tif (!options.prefer_surface_cache)', start)
selection = source[start:end]
with tempfile.TemporaryDirectory(prefix='rpcs3-texture-lookup-tests-') as directory:
    temp = Path(directory)
    (temp / 'TextureCollectionUnderTest.inc').write_text(collection)
    (temp / 'TextureSelectionUnderTest.inc').write_text(selection)
    executable = temp / 'TextureLookupTests'
    command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I', str(temp), str(HERE / 'TextureLookupTests.cpp'), '-o', str(executable)]
    if args.sanitize:
        command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], check=True)
