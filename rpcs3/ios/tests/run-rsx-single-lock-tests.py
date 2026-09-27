#!/usr/bin/env python3
"""Execute the production single-range lock path against its eight-change baseline."""
from pathlib import Path
import argparse,os,re,subprocess,tempfile
HERE=Path(__file__).resolve().parent
CORE=Path(os.environ.get('RPCS3_TEST_SOURCE_ROOT',HERE.parents[2]))
parser=argparse.ArgumentParser()
parser.add_argument('--sanitize',action='store_true')
parser.add_argument('--benchmark',action='store_true')
args=parser.parse_args()
def strip(s):return re.sub(r'^#(?:include|pragma once).*\n','',s,flags=re.M)
with tempfile.TemporaryDirectory(prefix='rsx-single-lock-') as d:
    p=Path(d)
    asm=(CORE/'rpcs3/util/asm.hpp').read_text()
    wait=asm[asm.index('\tinline void pause()'):asm.index('\n#ifdef ARCH_X64\n\tinline u64 get_wait_cycles')]
    (p/'sync-wait.inc').write_text('#define ARCH_ARM64 1\n'+strip((CORE/'rpcs3/util/tsc.hpp').read_text())+'\nnamespace utils {\n'+wait+'\n}\nusing utils::busy_wait;\n')
    for version in ('old','new'):
        mutex=strip((CORE/'Utilities/mutex.h').read_text());mutex=mutex[:mutex.index('// Simplified shared')]
        (p/(version+'-mutex.inc')).write_text(mutex+'\n'+strip((CORE/'Utilities/mutex.cpp').read_text()))
        path=HERE/'GTA5SyncReference/IOMapAccepted8.hpp' if version=='old' else CORE/'rpcs3/Emu/RSX/Core/RSXIOMap.hpp'
        (p/(version+'-rsx.inc')).write_text(strip(path.read_text())+'\ninline rsx::rsx_iomap_table::rsx_iomap_table() noexcept = default;\nstruct renderer_t {rsx::rsx_iomap_table iomap_table;};\ninline renderer_t renderer;\n')
    cmd=[os.environ.get('CXX','clang++'),'-std=c++20','-O2','-pthread','-Wall','-Wextra','-Werror',str(HERE/'RSXSingleLockTests.cpp'),'-I',str(p),'-o',str(p/'test')]
    if args.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run(cmd,check=True)
    subprocess.run([str(p/'test')]+(['benchmark'] if args.benchmark else []),check=True)
