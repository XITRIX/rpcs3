#!/usr/bin/env python3
from pathlib import Path
import argparse,subprocess,tempfile,os
HERE=Path(__file__).resolve().parent;CORE=HERE.parents[2]
p=argparse.ArgumentParser();p.add_argument('--candidate-root',type=Path,default=CORE);p.add_argument('--benchmark',action='store_true');p.add_argument('--sanitize',action='store_true');args=p.parse_args()
def block(s,marker):
 a=s.index(marker);b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
with tempfile.TemporaryDirectory(prefix='minecraft-index-') as d:
 t=Path(d);s=(args.candidate_root/'rpcs3/Emu/RSX/RSXOffload.cpp').read_text();ref=HERE/'MinecraftReference'
 (t/'new-dispatch.inc').write_text('__attribute__((noinline)) '+block(s,'void dma_manager::emulate_as_indexed('));(t/'old-dispatch.inc').write_text('__attribute__((noinline)) '+(ref/'index-dispatch.inc').read_text())
 for name in ['transport','index-generator']:(t/(name+'.inc')).write_bytes((ref/(name+'.inc')).read_bytes())
 cmd=[os.environ.get('CXX','clang++'),'-std=c++20','-O2','-Wall','-Wextra','-Werror','-pthread','-DRPCS3_IOS=1','-I',str(t),'-I',str(CORE),'-I',str(CORE/'rpcs3'),str(HERE/'MinecraftIndexTests.cpp'),'-o',str(t/'test')]
 if args.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
 subprocess.run(cmd,check=True);subprocess.run([str(t/'test'),*(['benchmark'] if args.benchmark else [])],check=True)
