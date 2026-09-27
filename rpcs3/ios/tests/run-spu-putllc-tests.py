#!/usr/bin/env python3
"""Differential execution of the production PUTLLC transaction and each change."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent
CORE=Path(os.environ.get("RPCS3_TEST_SOURCE_ROOT", HERE.parents[2]))
parser=argparse.ArgumentParser()
parser.add_argument('--sanitize',action='store_true')
args=parser.parse_args()
path='rpcs3/Emu/Cell/SPUThread.cpp'
base=(HERE/'GTA5BatchReference/Putllc.cpp').read_text()
new=(CORE/path).read_text()
def transaction(s):
    s=s[s.index('bool spu_thread::do_putllc('):]
    start=s.index('\t\tif (raddr != addr)')
    return s[start:s.index('\n\t}())')]
fence = new[new.index("static FORCE_INLINE bool rdata_fence()"):new.index("\n}", new.index("static FORCE_INLINE bool rdata_fence()"))+2].replace("FORCE_INLINE", "inline")
base=transaction(base);new=transaction(new)
scan='const usz diff16_pos = scan16_rdata(to_write, rdata);'
lazy='const bool relaxed_spurs = !accurate && addr - spurs_addr <= 0x80;\n\t\tconst usz diff16_pos = relaxed_spurs ? usz{umax} : scan16_rdata(to_write, rdata);'
assert lazy in new and scan in base
noop=new.replace(lazy,scan).replace('if (relaxed_spurs)','if (addr - spurs_addr <= 0x80)')
lazy_only=base.replace(scan,lazy.replace('!accurate','!g_cfg.core.spu_accurate_reservations')).replace('if (addr - spurs_addr <= 0x80)','if (relaxed_spurs)')
with tempfile.TemporaryDirectory(prefix='rpcs3-putllc-') as d:
    temp=Path(d)
    for variant,body in [('noop',noop),('lazy_scan',lazy_only),('combined',new)]:
        (temp/'Putllc.inc').write_text(fence+'\n'+'bool baseline(const spu_mfc_cmd& args) {const u32 addr=args.eal & -128;'+base+'}\n' +
                                     'bool candidate(const spu_mfc_cmd& args) {const u32 addr=args.eal & -128;'+body+'}\n')
        exe=temp/'test'
        cmd=[os.environ.get('CXX','clang++'),'-std=c++20','-O2','-Wall','-Wextra','-Werror','-I',str(temp),str(HERE/'SPUPutllcTests.cpp'),'-o',str(exe)]
        if args.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        subprocess.run(cmd,check=True)
        subprocess.run([str(exe),variant],check=True)
