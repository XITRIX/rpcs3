#!/usr/bin/env python3
"""Exercise the real perf_meter template with a counted clock and report sink."""
from pathlib import Path
import argparse
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
CORE = HERE.parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sanitize', action='store_true')
a = p.parse_args()
source = (CORE / 'Emu/perf_meter.hpp').read_text()
# Include the complete production template, without pulling the emulator runtime
# into this clock/report policy test. No constructor or destructor is re-created.
meter = source[source.index('template <auto ShortName, auto... SubEvents>'):]
for file, tag in [('Emu/Memory/vm.cpp', 'SUSPEND'), ('Emu/Cell/SPUThread.cpp', 'PUTLLUC')]:
    assert f'perf_meter<"{tag}"_u64> perf0(nullptr);' in (CORE / file).read_text()
with tempfile.TemporaryDirectory(prefix='rpcs3-perf-policy-') as temp:
    root = Path(temp)
    (root / 'PerfMeter.inc').write_text(meter)
    command = ['xcrun', 'clang++', '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I', str(root), str(HERE / 'ReservationPerfMeterTests.cpp'), '-o', str(root / 'tests')]
    if a.sanitize:
        command[2:2] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(command, check=True)
    subprocess.run([str(root / 'tests')], check=True)
