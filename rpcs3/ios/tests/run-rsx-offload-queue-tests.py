#!/usr/bin/env python3
"""Execute production queue runners and synchronization with contending host threads."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--negative-control', action='store_true')
args = parser.parse_args()
header = (ROOT / 'rpcs3/Emu/RSX/RSXOffload.h').read_text()
header = re.sub(r'^#(?:include|pragma).*$', '', header, flags=re.M)
header = header.replace('class dma_manager\n\t{', 'class dma_manager\n\t{\n\tpublic:')
source = (ROOT / 'rpcs3/Emu/RSX/RSXOffload.cpp').read_text()
worker = source[source.index('\tstruct dma_manager::offload_thread'):source.index('\n\t// initialization')]
methods = source[source.index('\t// General transport'):source.index('\n\tvoid dma_manager::set_mem_fault_flag')]
if args.negative_control:
    methods = methods.replace('if (run_here && _thr.m_exec_claim.compare_and_swap_test(0, 1))',
                              'if (false && run_here && _thr.m_exec_claim.compare_and_swap_test(0, 1))')
with tempfile.TemporaryDirectory(prefix='rsx-offload-queue-') as directory:
    temp = Path(directory)
    (temp / 'production.h').write_text(header)
    (temp / 'production.inc').write_text('namespace rsx {\n' + worker + methods + '\n}')
    exe = temp / 'tests'
    cmd = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-O2', '-pthread',
           '-Wall', '-Wextra', '-Werror', '-I', str(temp),
           str(HERE / 'RSXOffloadQueueTests.cpp'), '-o', str(exe)]
    if args.sanitize:
        cmd[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(cmd, check=True)
    result = subprocess.run([str(exe)], timeout=30, capture_output=args.negative_control)
    if args.negative_control:
        assert result.returncode != 0, 'Original waiting-only path unexpectedly drains a parked queue'
        print('Negative control rejected: a parked worker cannot complete the FIFO handoff.')
    else:
        result.check_returncode()
