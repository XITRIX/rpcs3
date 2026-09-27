#!/usr/bin/env python3
"""Differential execution of MFC masks and interrupt dispatch from production."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile
HERE=Path(__file__).resolve().parent
CORE=HERE.parents[2]
CORE = Path(os.environ.get("RPCS3_TEST_SOURCE_ROOT", CORE))
p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');p.add_argument('--benchmark',action='store_true');args=p.parse_args()
def block(s,marker):
    start=s.index(marker);i=s.index('{',start)+1;depth=1
    while depth:
        depth+=(s[i]=='{')-(s[i]=='}');i+=1
    return s[start:i]
new=(CORE/'rpcs3/Emu/Cell/SPUThread.cpp').read_text()
base=(HERE/'GTA5SyncReference/MFC.inc').read_text()
with tempfile.TemporaryDirectory(prefix='spu-mfc-fast-') as temp:
    t=Path(temp)
    for tag,source in [('old',base),('new',new)]:
        mask=block(source,'auto get_exec_mask = [&size = mfc_size]').split('{',1)[1]
        irq=block(source,'bool spu_thread::check_mfc_interrupts(').replace('spu_thread::check_mfc_interrupts','interrupt_'+tag)
        (t/(tag+'-mfc.inc')).write_text('__attribute__((noinline)) u16 mask_'+tag+'(u32 size) {'+mask+'\n'+irq)
    exe=t/'test';cmd=[os.environ.get('CXX','clang++'),'-std=c++20','-O2','-Wall','-Wextra','-Werror','-I',str(t),str(HERE/'SPUMFCFastTests.cpp'),'-o',str(exe)]
    if args.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run(cmd,check=True);subprocess.run([str(exe)]+(['benchmark'] if args.benchmark else []),check=True)
