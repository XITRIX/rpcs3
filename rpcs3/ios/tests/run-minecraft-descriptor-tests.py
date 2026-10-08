#!/usr/bin/env python3
from pathlib import Path
import argparse, subprocess, tempfile, os
HERE=Path(__file__).resolve().parent; CORE=HERE.parents[2]
p=argparse.ArgumentParser(); p.add_argument('--candidate-root',type=Path,default=CORE); p.add_argument('--benchmark',action='store_true'); p.add_argument('--sanitize',action='store_true'); args=p.parse_args()
def block(s,marker):
 a=s.index(marker); b=s.index('{',a)+1; depth=1
 while depth: depth+=(s[b]=='{')-(s[b]=='}'); b+=1
 return s[a:b]
with tempfile.TemporaryDirectory(prefix='minecraft-descriptors-') as d:
 t=Path(d); h=(args.candidate_root/'rpcs3/Emu/RSX/VK/VKProgramPipeline.h').read_text(); cpp=(args.candidate_root/'rpcs3/Emu/RSX/VK/VKProgramPipeline.cpp').read_text(); ref=HERE/'MinecraftReference'
 (t/'types.inc').write_bytes((ref/'descriptor-types.inc').read_bytes())
 for typ in ['ImageInfo','BufferInfo','BufferView']:
  name='VkDescriptor'+typ+'Ex'; old=(ref/('descriptor-bind-'+typ+'.inc')).read_text()
  eq_marker='bool operator == (const descriptor_slot_t& a, const '+name+'& b)'
  (t/('old-eq-'+typ+'.inc')).write_bytes((ref/('descriptor-eq-'+typ+'.inc')).read_bytes())
  (t/('new-eq-'+typ+'.inc')).write_text('inline '+block(h if 'inline bool operator ==' in h else cpp,eq_marker))
  (t/('old-bind-'+typ+'.inc')).write_text('__attribute__((noinline)) '+old)
  marker='void bind_uniform(const '+name
  new=block(h,marker) if 'inline bool operator ==' in h else old.replace('program::','')
  (t/('new-bind-'+typ+'.inc')).write_text(new)
 cmd=[os.environ.get('CXX','clang++'),'-std=c++20','-O2','-Wall','-Wextra','-Werror','-I',str(t),'-I',str(CORE/'rpcs3'),str(HERE/'MinecraftDescriptorTests.cpp'),'-o',str(t/'test')]
 if args.sanitize: cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
 subprocess.run(cmd,check=True); subprocess.run([str(t/'test'),*(['benchmark'] if args.benchmark else [])],check=True)
