#!/usr/bin/env python3
"""Exercise actual Vulkan instance functions with an injected driver boundary."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, default=ROOT / 'rpcs3/Emu/RSX/VK/vkutils/instance.cpp')
parser.add_argument('--desktop', action='store_true')
parser.add_argument('--callback-only', action='store_true')
args = parser.parse_args()
source = args.source.read_text()
debug = source[source.index('\tvoid instance::enable_debugging()'):source.index('\n#ifdef __clang__', source.index('\tvoid instance::enable_debugging()'))]
create = source[source.index('\tbool instance::create('):source.index('\n#ifdef __clang__', source.index('\tbool instance::create('))]
with tempfile.TemporaryDirectory(prefix='vulkan-instance-') as directory:
    out = Path(directory)
    (out / 'VulkanInstanceUnderTest.inc').write_text(debug + '\n' + create)
    headers = ROOT / '3rdparty/libsdl-org/SDL/src/video/khronos'
    command = [*shlex.split(os.environ.get('CXX', 'clang++')), '-std=c++20', '-O2', '-g',
               '-I' + str(headers), '-iquote', str(out),
               *([] if args.desktop else ['-DRPCS3_IOS=1']),
               *shlex.split(os.environ.get('VULKAN_INSTANCE_CXXFLAGS', '')),
               str(HERE / 'VulkanInstanceDiagnosticsTests.cpp'), '-o', str(out / 'tests')]
    subprocess.run(command, check=True)
    subprocess.run([str(out / 'tests'), *(['--callback-only'] if args.callback_only else [])], check=True)
