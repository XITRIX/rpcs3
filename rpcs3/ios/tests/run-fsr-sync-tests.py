#!/usr/bin/env python3
"""Execute production FSR setup/scale paths with a recorded Vulkan command stream.

This validates emitted dependencies and formats, not GPU/driver execution.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
parser=argparse.ArgumentParser()
parser.add_argument('--source',type=Path,default=ROOT/'rpcs3/Emu/RSX/VK/upscalers/fsr1/fsr_pass.cpp')
parser.add_argument('--present-source',type=Path,default=ROOT/'rpcs3/Emu/RSX/VK/VKPresent.cpp')
parser.add_argument('--sanitize',action='store_true')
args=parser.parse_args()
src=args.source.read_text()
code=src[src.index('\tvoid fsr_upscale_pass::dispose_images()'):]
present=args.present_source.read_text()
start=present.index('if (g_cfg.video.record_with_overlays && has_overlay)')
key_start=present.index('const auto key =',start)
key_end=present.index(';',key_start)+1
with tempfile.TemporaryDirectory(prefix='fsr-sync-') as temp:
    out=Path(temp);(out/'production.inc').write_text('namespace vk {\n'+code)
    (out/'capture.inc').write_text(present[key_start:key_end]+'\nreturn key;')
    command=[os.environ.get('CXX','clang++'),'-std=c++20','-O2','-Wall','-Wextra','-Werror',
             '-I',str(out),'-I',str(ROOT/'3rdparty/libsdl-org/SDL/src/video/khronos'),
             str(HERE/'FSRSyncTests.cpp'),'-o',str(out/'tests')]
    if args.sanitize: command[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run(command,check=True);subprocess.run([out/'tests'],check=True)
