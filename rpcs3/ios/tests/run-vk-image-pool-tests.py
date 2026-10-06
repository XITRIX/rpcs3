#!/usr/bin/env python3
"""Exercise production image-pool ownership during an interleaved driver return.

GPU disposal is stubbed; the cache methods and deque ownership are production code.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, default=ROOT / 'rpcs3/Emu/RSX/VK/VKTextureCache.cpp')
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--negative-control', action='store_true')
args = parser.parse_args()
source = args.source.read_text()


def block(marker):
    start = source.index(marker)
    pos = source.index('{', start)
    depth = 1
    end = pos + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


markers = (
    'u64 hash_image_properties(',
    'texture_cache::cached_image_reference_t::cached_image_reference_t(',
    'texture_cache::cached_image_reference_t::~cached_image_reference_t()',
    'void texture_cache::clear()',
    'std::unique_ptr<vk::viewable_image> texture_cache::find_cached_image(',
    'bool texture_cache::handle_memory_pressure(',
    'void texture_cache::on_frame_end()',
    'u32 texture_cache::get_unreleased_textures_count() const',
)
with tempfile.TemporaryDirectory(prefix='rpcs3-vk-image-pool-') as directory:
    temp = Path(directory)
    (temp / 'VKImagePool.inc').write_text('namespace vk {\n' + '\n'.join(map(block, markers)) + '\n}')
    binary = temp / 'tests'
    command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-O2', '-pthread',
               '-Wall', '-Wextra', '-Werror', '-I', str(temp),
               str(HERE / 'VKImagePoolTests.cpp'), '-o', str(binary)]
    if args.sanitize:
        command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(command, check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
    if args.negative_control:
        if result.returncode == 0 or 'Image-pool ownership/accounting assertion failed' not in result.stderr:
            raise SystemExit('Expected original cleanup to lose the interleaved driver return:\n' + result.stdout + result.stderr)
        print('Original cleanup negative control loses the interleaved driver return as expected')
    else:
        print(result.stdout, end='')
        if result.returncode:
            raise SystemExit(result.stderr)
