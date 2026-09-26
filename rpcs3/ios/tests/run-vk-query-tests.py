#!/usr/bin/env python3
"""Execute each query optimization separately against the pre-batch implementation."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
CORE = HERE.parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()

def block(text, marker):
    start = text.index(marker)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

path = 'rpcs3/Emu/RSX/VK/VKQueryPool.cpp'
current = (CORE / path).read_text()
baseline = (HERE/'GTA5BatchReference/Query.cpp').read_text()
header = (CORE / 'rpcs3/Emu/RSX/VK/VKQueryPool.h').read_text()
old_header = (HERE/'GTA5BatchReference/Query.h').read_text()
with tempfile.TemporaryDirectory(prefix='rpcs3-query-tests-') as d:
    temp = Path(d)
    for variant in ('baseline', 'cached', 'wait', 'release', 'combined'):
        chunks = []
        for marker, candidate in [('inline bool query_pool_manager::poke_query(', False),
                                  ('bool query_pool_manager::check_query_status(', variant in ('cached', 'combined')),
                                  ('u32 query_pool_manager::get_query_result(', variant in ('wait', 'combined')),
                                  ('void query_pool_manager::free_query(', variant in ('release', 'combined'))]:
            chunks.append(block(current if candidate else baseline, marker))
        if variant in ('release', 'combined'):
            chunks.append(block(current, 'bool query_pool_manager::release_query('))
        (temp/'QueryMethods.inc').write_text('\n'.join(chunks))
        (temp/'QueryFree.inc').write_text('template<typename T>\n'+block(header if variant in ('release', 'combined') else old_header, 'void free_queries('))
        exe = temp/'test'
        command = [os.environ.get('CXX','clang++'), '-std=c++20','-O2','-Wall','-Wextra','-Werror',
                   '-I',str(temp), str(HERE/'VKQueryTests.cpp'),'-o',str(exe)]
        if args.sanitize:
            command[1:1] = ['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        subprocess.run(command,check=True)
        subprocess.run([str(exe),variant],check=True)
