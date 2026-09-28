#!/usr/bin/env python3
"""Exercise the production framebuffer selector with surface metadata fixtures."""
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
start = source.index('\t\ttemplate <typename OverlapList>')
end = source.index('\n\t\tstatic inline bool force_strict_fbo_sampling', start)
selector = source[start:end]
cache = (ROOT / 'rpcs3/Emu/RSX/Common/texture_cache.h').read_text()
assert 'helpers::select_framebuffer_source(overlapping_fbos, helpers::is_gcm_depth_format(attr.gcm_format))' in cache
if args.negative_control:
    selector = '''template <typename OverlapList>
const auto& select_framebuffer_source(const OverlapList& overlaps, bool)
{ return overlaps.back(); }
'''
with tempfile.TemporaryDirectory(prefix='rpcs3-framebuffer-source-tests-') as directory:
    temp = Path(directory)
    (temp / 'FramebufferSourceUnderTest.inc').write_text(selector)
    executable = temp / 'FramebufferSourceTests'
    command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I', str(temp), str(HERE / 'FramebufferSourceTests.cpp'), '-o', str(executable)]
    if args.sanitize:
        command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(command, check=True)
    result = subprocess.run([str(executable)], capture_output=args.negative_control)
    if args.negative_control:
        assert result.returncode != 0, 'Original last-candidate policy unexpectedly passed'
        assert b'Assertion failed' in result.stderr or b'Assertion' in result.stderr
        print('Negative control: original selection fails the mirror regression as expected')
    else:
        result.check_returncode()
