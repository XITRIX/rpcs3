#!/usr/bin/env python3
"""Execute production buffered_section methods with a small host memory adapter."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
CORE = HERE.parents[2]
p = argparse.ArgumentParser()
p.add_argument('--sanitize', action='store_true')
args = p.parse_args()
header = (CORE / 'rpcs3/Emu/RSX/Common/texture_cache_utils.h').read_text()
source = (CORE / 'rpcs3/Emu/RSX/Common/texture_cache.cpp').read_text()
class_start = header.index('\tclass buffered_section\n')
class_end = header.index('\n\t/**\n\t * Cached Texture Section', class_start)
methods_start = source.index('\tvoid buffered_section::init_lockable_range')
methods_end = source.index('\n\tnamespace blit_engine_helpers', methods_start)
secret_start = source.index('\nnamespace\n{')
secret_end = source.index('\n#endif', secret_start)
with tempfile.TemporaryDirectory(prefix='texture-section-') as directory:
    output = Path(directory)
    (output / 'section.inc').write_text(source[secret_start:secret_end] + '\nnamespace rsx {\n' +
        header[class_start:class_end] + source[methods_start:methods_end] + '\n}\n')
    exe = output / 'test'
    command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
               '-DRPCS3_IOS=1', '-I', str(CORE), '-I', str(CORE / 'rpcs3'), '-I', str(output),
               str(HERE / 'TextureSectionTests.cpp'), str(CORE / 'rpcs3/ios/IOSTextureHash.cpp'), '-o', str(exe)]
    if args.sanitize:
        command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(command, check=True)
    subprocess.run([str(exe)], check=True)
