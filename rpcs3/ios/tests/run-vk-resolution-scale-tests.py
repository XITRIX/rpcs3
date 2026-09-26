#!/usr/bin/env python3
"""Run production scale-transition and framebuffer reuse gates with fake GPU resources.

This checks CPU binding lifetime/invalidation, not Vulkan execution or GPU completion.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=ROOT / 'rpcs3/Emu/RSX/VK/VKPresent.cpp')
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
present = args.source.read_text()
renderer = (ROOT / 'rpcs3/Emu/RSX/VK/VKGSRender.cpp').read_text()
draw = (ROOT / 'rpcs3/Emu/RSX/VK/VKDraw.cpp').read_text()
flags = (ROOT / 'rpcs3/Emu/RSX/Core/RSXDriverState.h').read_text()

transition = present[present.index('\t// Data sync\n'):].rstrip()
assert transition.endswith('}')
prepare_start = renderer.index('void VKGSRender::prepare_rtts(')
prepare_end = renderer.index('\n\tm_rtts.prepare_render_target(', prepare_start)
prepare = renderer[prepare_start:prepare_end]
enums = flags[flags.index('namespace rsx'):]
close_start = draw.index('void VKGSRender::close_render_pass()')
close_end = draw.index('\n}', close_start) + 2

with tempfile.TemporaryDirectory(prefix='rpcs3-vk-scaling-') as directory:
    temp = Path(directory)
    (temp / 'VKScalingFlags.inc').write_text(enums)
    (temp / 'VKScalingUnderTest.inc').write_text(
        draw[close_start:close_end] + '\n\n' +
        'void VKGSRender::sync_config()\n{\n' + transition + '\n\n' + prepare +
        '\n\trebuild_framebuffer(clipped_scissor);\n}\n')
    executable = temp / 'VKResolutionScaleTests'
    command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-O2',
               '-Wall', '-Wextra', '-Werror', '-I', str(temp), '-I', str(ROOT / 'rpcs3'),
               str(HERE / 'VKResolutionScaleTests.cpp'), '-o', str(executable)]
    if args.sanitize:
        command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], check=True)
