#!/usr/bin/env python3
"""Isolated executable checks for binding iteration, pipeline insertion and slot FIFO."""
from pathlib import Path
import argparse
import os
import re
import subprocess
import tempfile
HERE=Path(__file__).resolve().parent;CORE=HERE.parents[2]
p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');args=p.parse_args()
def baseline(path):return (HERE/'GTA5BatchReference'/('Draw.cpp' if path.endswith('VKDraw.cpp') else 'Pipeline.h')).read_text()
drawpath='rpcs3/Emu/RSX/VK/VKDraw.cpp'
draw=(CORE/drawpath).read_text();old=baseline(drawpath)
def iteration(s):
    start=s.index('for (u32 textures_ref = current_fp_metadata.referenced_textures_mask')
    end=s.index('\n\n\t\tif (!fs_sampler_state',start)
    return s[start:end].replace('current_fp_metadata.referenced_textures_mask','mask')
def insert(s):
    # Only the existing writer-locked placeholder publication block.
    start=s.index('// Check if another submission completed' if 'Check if another submission completed' in s else '// Atomically find or insert')
    return s[start:s.index('\n\t\t}',start)]
path='rpcs3/Emu/RSX/Program/ProgramStateCache.h'
with tempfile.TemporaryDirectory(prefix='rpcs3-cache-batch-') as d:
    t=Path(d)
    for name,s in [('old',old),('new',draw)]:
        (t/(name+'-iteration.inc')).write_text(iteration(s)+'\n result.push_back(i);\n}\n')
    assert draw.count('textures_ref &= textures_ref - 1')==5
    assert 'textures_ref >>= 1' not in draw
    for name,s in [('old',baseline(path)),('new',(CORE/path).read_text())]:
        (t/(name+'-pipeline.inc')).write_text(insert(s))
    exe=t/'test';cmd=[os.environ.get('CXX','clang++'),'-std=c++20','-O2','-Wall','-Wextra','-Werror','-I',str(t),'-I',str(CORE/'rpcs3'),str(HERE/'RSXCacheBatchTests.cpp'),'-o',str(exe)]
    if args.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run(cmd,check=True)
    for mode in ('iteration','pipeline','queue'):subprocess.run([str(exe),mode],check=True)
