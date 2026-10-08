#!/usr/bin/env python3
from pathlib import Path
import argparse,subprocess,tempfile,os
HERE=Path(__file__).resolve().parent;CORE=HERE.parents[2]
p=argparse.ArgumentParser();p.add_argument('--candidate-root',type=Path,default=CORE);p.add_argument('--benchmark',action='store_true');p.add_argument('--sanitize',action='store_true');p.add_argument('--native-atomic',action='store_true');args=p.parse_args()
def block(s,marker):
 a=s.index(marker);b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
with tempfile.TemporaryDirectory(prefix='minecraft-fence-') as d:
 t=Path(d);s=(args.candidate_root/'rpcs3/Emu/RSX/VK/vkutils/sync.cpp').read_text()
 for name in ['wait_flush','signal_flushed']:
  (t/('new-'+name+'.inc')).write_text(block(s,'void fence::'+name+'()'))
  (t/('old-'+name+'.inc')).write_bytes((HERE/'MinecraftReference'/('fence-'+name+'.inc')).read_bytes())
 cmd=[os.environ.get('CXX','clang++'),'-std=c++20','-O2','-Wall','-Wextra','-Werror','-pthread','-DRPCS3_IOS=1','-I',str(t),str(HERE/'MinecraftFenceTests.cpp'),'-o',str(t/'test')]
 if args.native_atomic:cmd[1:1]=['-DMINECRAFT_NATIVE_ATOMIC=1','-I',str(CORE),'-I',str(CORE/'rpcs3'),str(CORE/'rpcs3/util/atomic.cpp'),str(HERE/'MinecraftAtomicSupport.cpp')]
 if args.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
 subprocess.run(cmd,check=True);subprocess.run([str(t/'test'),*(['benchmark'] if args.benchmark else [])],check=True,timeout=90)
