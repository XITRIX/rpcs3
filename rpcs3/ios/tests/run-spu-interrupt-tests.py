#!/usr/bin/env python3
"""Execute production SPU timer, interrupt dispatch and pending-work methods with a fake clock."""
import argparse, os, subprocess, tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=HERE.parents[1]/'Emu/Cell/SPUThread.cpp');p.add_argument('--sanitize',action='store_true');a=p.parse_args()
s=a.source.read_text()
def method(signature):
    start=s.index(signature); opening=s.index('{',start); depth=1;end=opening+1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[start:end]
parts=[s[s.index('struct spu_dec_intr_timer'):s.index('\nu32 spu_thread::get_ch_count')]]
parts += [method(x) for x in ['std::pair<u32, u32> spu_thread::read_dec()', 'bool spu_thread::check_mfc_interrupts(', 'void spu_thread::cpu_work()']]
with tempfile.TemporaryDirectory(prefix='spu-interrupt-') as tmp:
    out=Path(tmp);(out/'production.inc').write_text('\n'.join(parts))
    cmd=[os.environ.get('CXX','clang++'),'-std=c++20','-O2','-Wall','-Wextra','-Werror','-I',str(out),str(HERE/'SPUCompiledInterruptTests.cpp'),'-o',str(out/'tests')]
    if a.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run(cmd,check=True);subprocess.run([out/'tests'],check=True)
