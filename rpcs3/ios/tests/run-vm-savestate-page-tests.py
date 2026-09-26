#!/usr/bin/env python3
"""Exercise production restore/allocation flags and unmap validation on page metadata.

Native VM mappings and serialization I/O are outside this host fixture.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=ROOT / 'rpcs3/Emu/Memory/vm.cpp')
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
source = args.source.read_text()
header = (ROOT / 'rpcs3/Emu/Memory/vm.h').read_text()


def between(text, start, end, offset=0):
    begin = text.index(start, offset)
    return text[begin:text.index(end, begin)]


enums = ''
for name in ('page_info_t', 'block_flags_3', 'alloc_flags'):
    enums += between(header, '\tenum ' + name, '\n\t};') + '\n\t};\n'
restore = between(source, '\t\t\tu64 pflags = 0;', '\n\t\t\t// Map the memory',
                  source.index('block_t::block_t(utils::serial&'))
allocate = between(source, '\t\tu8 flags = 0;', '\n\t\tif (this->flags & stack_guarded)',
                   source.index('bool block_t::try_alloc'))
scan = between(source, '\t\t// Determine deallocation size', '\n\t\tconst bool is_exec',
               source.index('static u32 _page_unmap'))

with tempfile.TemporaryDirectory(prefix='rpcs3-vm-savestate-') as directory:
    temp = Path(directory)
    (temp / 'VMSavestatePagesUnderTest.inc').write_text(
        enums + '\nu64 restore_flags(u64 flags, u8 flags0) {\n' + restore +
        '\nreturn pflags;\n}\nu8 allocation_flags(u64 bflags) {\n' + allocate +
        '\nreturn flags | page_allocated;\n}\nu32 unmap_size(u32 addr, u32 max_size) {\n' +
        scan + '\nreturn size;\n}\n')
    executable = temp / 'VMSavestatePageTests'
    command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-O2',
               '-Wall', '-Wextra', '-Werror', '-I', str(temp),
               str(HERE / 'VMSavestatePageTests.cpp'), '-o', str(executable)]
    if args.sanitize:
        command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], check=True)
