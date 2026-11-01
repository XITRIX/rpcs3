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

# Model the context/resource accessors in the existing fake GPU fixture.
# Preserve production transition ordering and the backend's reuse condition.
transition = present[present.index('\t// Data sync\n'):].rstrip()
assert transition.endswith('}')
for actual, fixture in (
    ('res()->surface_cache', 'm_rtts'), ('res() && ', ''),
    ('m_rtts.scaling_config()', 'resolution_scaling_config'),
    ('m_ctx->graphics_state', 'm_graphics_state'),
    ('m_fs_sampler_descriptors', 'fs_sampler_state'),
    ('m_vs_sampler_descriptors', 'vs_sampler_state'),
    ('m_ctx->textures.fragment_textures_dirty', 'm_textures_dirty'),
    ('m_ctx->textures.vertex_textures_dirty', 'm_vertex_textures_dirty'),
    ('RSX(m_ctx)->resolution_scaling_config', 'resolution_scaling_config')):
    transition = transition.replace(actual, fixture)
prepare_start = renderer.index('bool VKGSRender::on_framebuffer_bound(')
prepare_end = renderer.index('\n\tres()->surface_cache.prepare_render_target(', prepare_start)
prepare = renderer[prepare_start:prepare_end]
# The graph engine owns validity now. The fixture calls the production backend
# gate only for a valid graph layout, then models allocation below that gate.
prepare = prepare.replace('bool VKGSRender::on_framebuffer_bound(const rsx::framebuffer_layout& layout)',
    'void VKGSRender::prepare_rtts(Context context)')
prepare = prepare.replace('{', '{\n\tif (!valid_layout) return;\n\tget_framebuffer_layout(context, m_framebuffer_layout);\n\tconst auto& layout = m_framebuffer_layout;\n\tconst bool clipped_scissor = true;', 1)
prepare = prepare.replace('return true;', 'return;')
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
