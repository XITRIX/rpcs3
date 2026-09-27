#!/usr/bin/env python3
"""Differential production PUTLLC execution with atomic configuration-read counts."""
from pathlib import Path
import argparse,os,subprocess,tempfile
HERE=Path(__file__).resolve().parent
CORE=Path(os.environ.get('RPCS3_TEST_SOURCE_ROOT',HERE.parents[2]))
parser=argparse.ArgumentParser()
parser.add_argument('--sanitize',action='store_true')
parser.add_argument('--benchmark',action='store_true')
args=parser.parse_args()
old=(HERE/'GTA5SyncReference/Putllc.inc').read_text()
new=(CORE/'rpcs3/Emu/Cell/SPUThread.cpp').read_text()
new=new[new.index('bool spu_thread::do_putllc('):]
new=new[new.index('\t\tif (raddr != addr)'):new.index('\n\t}())')]
with tempfile.TemporaryDirectory(prefix='putllc-config-') as d:
    p=Path(d)
    (p/'Putllc.inc').write_text('bool baseline(const spu_mfc_cmd& args){const u32 addr=args.eal & -128;'+old+'}\n'+'bool candidate(const spu_mfc_cmd& args){const u32 addr=args.eal & -128;'+new+'}\n')
    cmd=[os.environ.get('CXX','clang++'),'-std=c++20','-O2','-Wall','-Wextra','-Werror','-DCOUNT_CONFIG_READS','-I',str(p),str(HERE/'SPUPutllcConfigTests.cpp'),'-o',str(p/'test')]
    if args.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run(cmd,check=True);subprocess.run([str(p/'test'),'cached_config'],check=True)
    if args.benchmark:
        cmd.remove('-DCOUNT_CONFIG_READS');cmd.insert(1,'-DRPCS3_PUTLLC_BENCH')
        subprocess.run(cmd,check=True);subprocess.run([str(p/'test')],check=True)
