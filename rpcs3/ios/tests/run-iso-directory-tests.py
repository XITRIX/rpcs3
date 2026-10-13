#!/usr/bin/env python3
"""Execute the production ISO record parser and LV2 directory ordering."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=HERE.parents[1] / 'Loader/ISO.cpp')
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
src = args.source.read_text()
lv2 = (HERE.parents[1] / 'Emu/Cell/lv2/sys_fs.cpp').read_text()
header = (HERE.parents[1] / 'Loader/ISO.h').read_text()

def section(text, start, end):
    begin = text.index(start)
    return text[begin:text.index(end, begin)]

code = section(header, 'struct iso_extent_info', 'class iso_file :')
code += section(src, 'template<typename T>\ninline T retrieve_endian_int', 'static bool iso_form_hierarchy')
code += section(src, 'bool iso_dir::read', '// Cuts the device prefix')
code += '\nvoid order_entries(std::vector<fs::dir_entry>& data)\n{\n'
code += section(lv2, '\t// Pull . and .. to the front', '\n\tif (const u32 id = idm::make<lv2_fs_object, lv2_dir>')
code += '\n}\n'

with tempfile.TemporaryDirectory(prefix='iso-directory-') as temp:
    out = Path(temp)
    (out / 'production.inc').write_text(code)
    command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I', str(out), str(HERE / 'ISODirectoryTests.cpp'), '-o', str(out / 'tests')]
    if args.sanitize:
        command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(command, check=True)
    subprocess.run([out / 'tests'], check=True)
