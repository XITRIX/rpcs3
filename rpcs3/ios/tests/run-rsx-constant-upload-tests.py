#!/usr/bin/env python3
"""Compare the production constant uploader against a bitwise scalar oracle."""
from pathlib import Path
import argparse,os,subprocess,tempfile
HERE=Path(__file__).resolve().parent;CORE=HERE.parents[2]
CORE = Path(os.environ.get("RPCS3_TEST_SOURCE_ROOT", CORE))
p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');p.add_argument('--benchmark',action='store_true');args=p.parse_args()
def block(s,marker):
 a=s.index(marker);b=s.index('{',a)+1;d=1
 while d:d+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
with tempfile.TemporaryDirectory(prefix='rsx-constants-') as d:
 t=Path(d)
 for tag,s in [('old',(HERE/'GTA5SyncReference/Constants.inc').read_text()),('new',(CORE/'rpcs3/Emu/RSX/Program/ProgramStateCache.cpp').read_text())]:
  code=block(s,'static inline void write_fragment_constants_to_buffer_sse2(').replace('static inline void write_fragment_constants_to_buffer_sse2','__attribute__((noinline)) void upload_'+tag)
  (t/(tag+'-constants.inc')).write_text(code)
 exe=t/'test';cmd=[os.environ.get('CXX','clang++'),'-std=c++20','-O2','-I',str(t),'-I',str(CORE/'rpcs3'),'-I',str(CORE),str(HERE/'RSXConstantUploadTests.cpp'),'-o',str(exe)]
 if args.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
 subprocess.run(cmd,check=True);subprocess.run([str(exe)]+(['benchmark'] if args.benchmark else []),check=True)
