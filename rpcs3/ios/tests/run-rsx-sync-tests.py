#!/usr/bin/env python3
"""Execute production mutex/RSX lock bodies, with deterministic and threaded drivers."""
from pathlib import Path
import argparse
import os
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
CORE = HERE.parents[2]
CORE = Path(os.environ.get("RPCS3_TEST_SOURCE_ROOT", CORE))
parser = argparse.ArgumentParser()
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--benchmark', action='store_true')
args = parser.parse_args()

def strip(s):
    return re.sub(r'^#(?:include|pragma once).*\n', '', s, flags=re.M)

with tempfile.TemporaryDirectory(prefix='rsx-sync-') as temp:
    p = Path(temp)
    # Use the real ARM timer wait: replacing it with a single YIELD changes
    # contention behavior and invalidates comparisons of the slow path.
    tsc = strip((CORE/'rpcs3/util/tsc.hpp').read_text())
    asm = (CORE/'rpcs3/util/asm.hpp').read_text()
    wait = asm[asm.index('\tinline void pause()'):asm.index('\n#ifdef ARCH_X64\n\tinline u64 get_wait_cycles')]
    (p/'sync-wait.inc').write_text('#define ARCH_ARM64 1\n'+tsc+
        '\nnamespace utils {\n'+wait+'\n}\nusing utils::busy_wait;\n')
    for version in ('old', 'new'):
        base = HERE / 'GTA5SyncReference'
        paths = [base/'Mutex.h', base/'Mutex.cpp', base/'IOMap.hpp', base/'Reservation.hpp'] if version == 'old' else [
            CORE/'Utilities/mutex.h', CORE/'Utilities/mutex.cpp',
            CORE/'rpcs3/Emu/RSX/Core/RSXIOMap.hpp', CORE/'rpcs3/Emu/RSX/Core/RSXReservationLock.hpp']
        mutex, impl, iomap, reservation = map(lambda x: strip(x.read_text()), paths)
        mutex = mutex[:mutex.index('// Simplified shared')]
        (p/(version+'-mutex.inc')).write_text(mutex+'\n'+impl)
        (p/(version+'-rsx.inc')).write_text(iomap+'\n'+'''
        inline rsx::rsx_iomap_table::rsx_iomap_table() noexcept = default;
        struct renderer_t { rsx::rsx_iomap_table iomap_table; };
        inline renderer_t renderer;
        renderer_t* get_current_renderer() { ++renderer_reads; return &renderer; }
        '''+'\n'+reservation)
    exe = p/'tests'
    cmd = [os.environ.get('CXX','clang++'), '-std=c++20','-O2','-pthread','-Wall','-Wextra','-Werror',
           '-I', str(p), str(HERE/'RSXSyncTests.cpp'), '-o', str(exe)]
    if args.sanitize:
        cmd[1:1] = ['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run(cmd, check=True)
    subprocess.run([str(exe)] + (['benchmark'] if args.benchmark else []), check=True)
