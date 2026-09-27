#!/usr/bin/env python3
"""Exercise production ISO read_at bodies with faulting I/O and a checked cipher stub."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
HERE=Path(__file__).resolve().parent
parser=argparse.ArgumentParser()
parser.add_argument('--source',type=Path,default=HERE.parents[1]/'Loader/ISO.cpp')
parser.add_argument('--sanitize',action='store_true')
args=parser.parse_args()
src=args.source.read_text()
def section(start,end): return src[src.index(start):src.index(end,src.index(start))]
code=section('struct iso_sector','static void* get_aligned_buf')+section('u64 iso_file_encrypted::read_at','template<typename T>')+section('u64 iso_file::read_at','u64 iso_file::write')
with tempfile.TemporaryDirectory(prefix='iso-read-') as temp:
    out=Path(temp);(out/'production.inc').write_text(code)
    command=[os.environ.get('CXX','clang++'),'-std=c++20','-O2','-Wall','-Wextra','-Werror','-I',str(out),str(HERE/'ISOReadTests.cpp'),'-o',str(out/'tests')]
    if args.sanitize: command[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run(command,check=True);subprocess.run([out/'tests'],check=True)
