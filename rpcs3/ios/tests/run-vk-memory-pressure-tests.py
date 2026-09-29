#!/usr/bin/env python3
"""Exercise the production eviction-exclusion scan without a Vulkan device."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, default=ROOT / 'rpcs3/Emu/RSX/VK/VKGSRender.cpp')
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
source = args.source.read_text()
start = source.index('bool VKGSRender::on_vram_exhausted(')
start = source.index('\t\t\tstd::set<u32> exclusion_list;', start)
end = source.index('\n\t\t\t// Hold the secondary lock guard', start)

with tempfile.TemporaryDirectory(prefix='rpcs3-vk-pressure-') as directory:
    temp = Path(directory)
    (temp / 'VKMemoryPressureScan.inc').write_text(source[start:end])
    executable = temp / 'tests'
    command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-O2',
               '-Wall', '-Wextra', '-Werror', '-I', str(temp),
               str(HERE / 'VKMemoryPressureTests.cpp'), '-o', str(executable)]
    if args.sanitize:
        command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], check=True)
