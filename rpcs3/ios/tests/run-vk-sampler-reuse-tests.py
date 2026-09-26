#!/usr/bin/env python3
from pathlib import Path
import argparse
import os
import subprocess
import tempfile
HERE=Path(__file__).resolve().parent;CORE=HERE.parents[2]
p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');args=p.parse_args()
def block(s,marker):
    a=s.index(marker);b=s.index('{',a)+1;d=1
    while d:d+=(s[b]=='{')-(s[b]=='}');b+=1
    return s[a:b]
path='rpcs3/Emu/RSX/VK/VKResourceManager.h'
new=(CORE/path).read_text();old=(HERE/'GTA5BatchReference/Sampler.h').read_text()
with tempfile.TemporaryDirectory(prefix='rpcs3-sampler-reuse-') as d:
    t=Path(d)
    for name,s in [('old',old),('new',new)]:
        (t/(name+'-sampler.inc')).write_text(block(s,'vk::sampler* get_sampler(').replace('get_sampler(',name+'_sampler('))
    exe=t/'test';cmd=[os.environ.get('CXX','clang++'),'-std=c++20','-O2','-Wall','-Wextra','-Werror','-I',str(t),str(HERE/'VKSamplerReuseTests.cpp'),'-o',str(exe)]
    if args.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True)
