#!/usr/bin/env python3
"""Exercise production ZCULL recovery with real page faults and a FIFO pause race."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
CORE = HERE.parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=CORE / 'rpcs3/Emu/RSX/RSXZCULL.cpp')
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--negative-control', action='store_true')
args = parser.parse_args()

source = args.source.read_text()
start = source.index('\t\tvoid ZCULL_control::on_report_enqueued(')
end = source.index('\n\t\t// Conditional rendering helpers', start)
body = source[start:end]
if args.negative_control and not re.search(r'need_disable_optimizations = true;[^\n]*\n\s*m_pages_accessed\[location\] = true;', body):
    body = body.replace('need_disable_optimizations = true;',
                        'need_disable_optimizations = true;\n m_pages_accessed[location] = true;', 1)
with tempfile.TemporaryDirectory(prefix='zcull-fault-handoff-') as directory:
    temp = Path(directory)
    (temp / 'production.inc').write_text(body)
    binary = temp / 'tests'
    command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-O2', '-pthread',
               '-Wall', '-Wextra', '-Werror', '-I', str(temp),
               str(HERE / 'ZCULLFaultHandoffTests.cpp'), '-o', str(binary)]
    if args.sanitize:
        command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(command, check=True)
    environment = dict(os.environ)
    if args.sanitize:
        environment['ASAN_OPTIONS'] = 'handle_segv=0:handle_sigbus=0'
    result = subprocess.run([str(binary)], env=environment, timeout=10)
    if args.negative_control:
        assert result.returncode == 86, f'Expected the report/FIFO deadlock, got {result.returncode}'
        print('Original-source control rejected: renderer fault cannot recover while report fault waits for FIFO pause.')
    else:
        result.check_returncode()
