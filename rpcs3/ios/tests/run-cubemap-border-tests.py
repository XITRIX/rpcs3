#!/usr/bin/env python3
"""Run production cubemap region construction against poisoned border fixtures."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--negative-control', action='store_true')
args = parser.parse_args()
source = (ROOT / 'rpcs3/Emu/RSX/Common/texture_cache_helpers.h').read_text()
start = source.index('\t\ttemplate <typename Sections, typename Image, typename ScaleCoordinates>')
end = source.index('\n\t\ttemplate <typename sampled_image_descriptor, typename commandbuffer_type, typename render_target_type>', start)
helper = source[start:end]
cache = (ROOT / 'rpcs3/Emu/RSX/Common/texture_cache.h').read_text()
assert 'attributes.cubemap_border = !attributes.swizzled && tex.cubemap() && tex.border_type() == CELL_GCM_TEXTURE_BORDER_TEXTURE;' in cache
assert '(static_cast<u64>(cubemap_border) << 61)' in cache
assert 'if (attr.cubemap_border)' in source and 'append_bordered_cubemap_sections(sections, texptr->get_surface' in source
if args.negative_control:
    # Reproduce the former unwrap's origin and tightly stacked mip rectangles.
    helper = helper.replace('offset.x + 1', 'offset.x').replace('y + 1', 'y').replace('y += height + 2;', 'y += height;')
with tempfile.TemporaryDirectory(prefix='rpcs3-cubemap-border-') as directory:
    temp = Path(directory)
    (temp / 'CubemapBorderUnderTest.inc').write_text(helper)
    executable = temp / 'CubemapBorderTests'
    command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I', str(temp), str(HERE / 'CubemapBorderTests.cpp'), '-o', str(executable)]
    if args.sanitize:
        command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(command, check=True)
    result = subprocess.run([str(executable)], capture_output=args.negative_control)
    if args.negative_control:
        assert result.returncode != 0, 'Original border-inclusive copy unexpectedly passed'
        assert b'Assertion' in result.stderr
        print('Negative control: original cubemap coordinates ingest border sentinel texels as expected')
    else:
        result.check_returncode()
