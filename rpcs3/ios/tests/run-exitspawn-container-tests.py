#!/usr/bin/env python3
"""Execute production exitspawn handoff and PPU container selection with stub boot/FXO services."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--ppu-source', type=Path, default=ROOT / 'rpcs3/Emu/Cell/PPUModule.cpp')
parser.add_argument('--process-source', type=Path, default=ROOT / 'rpcs3/Emu/Cell/lv2/sys_process.cpp')
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
ppu = args.ppu_source.read_text()
process = args.process_source.read_text()
start = ppu.index('\n', ppu.index('\t\tg_ps3_process_info.ppc_seg = ppc_seg;'))
selection = ppu[start:ppu.index('\n\t\tvoid init_fxo_for_exec(', start)]
start = process.index('\t\t\tEmu.argv =', process.index('void lv2_exitspawn('))
handoff = process[start:process.index('\n\t\t};', start)]
with tempfile.TemporaryDirectory(prefix='rpcs3-exitspawn-containers-') as directory:
    temp = Path(directory)
    (temp / 'ContainerSelectionUnderTest.inc').write_text(selection)
    (temp / 'ExitspawnHandoffUnderTest.inc').write_text(handoff)
    executable = temp / 'ExitspawnContainerTests'
    command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-O2',
               '-Wall', '-Wextra', '-Werror', '-I', str(temp),
               str(HERE / 'ExitspawnContainerTests.cpp'), '-o', str(executable)]
    if args.sanitize:
        command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], check=True)
