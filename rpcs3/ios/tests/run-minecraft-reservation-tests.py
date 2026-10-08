#!/usr/bin/env python3
"""Verify inlined reservation operations against original separate-TU functions."""
from pathlib import Path
import argparse,os,subprocess,tempfile
HERE=Path(__file__).resolve().parent;CORE=HERE.parents[2]
p=argparse.ArgumentParser();p.add_argument('--candidate-root',type=Path,default=CORE);p.add_argument('--sanitize',action='store_true');p.add_argument('--benchmark',action='store_true');args=p.parse_args()
with tempfile.TemporaryDirectory(prefix='minecraft-reservation-') as d:
 t=Path(d);old='#include <arm_neon.h>\n#include "util/v128.hpp"\nusing spu_rdata_t = std::byte[128];\n'+(HERE/'MinecraftReference/cmp.inc').read_text()+'\n'+(HERE/'MinecraftReference/mov.inc').read_text()
 (t/'old.cpp').write_text(old)
 cmd=[os.environ.get('CXX','clang++'),'-std=c++20','-O2','-Wall','-Wextra','-Werror','-I',str(args.candidate_root/'rpcs3'),'-I',str(CORE/'rpcs3'),str(HERE/'MinecraftReservationTests.cpp'),str(t/'old.cpp'),'-o',str(t/'test')]
 if args.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
 subprocess.run(cmd,check=True);subprocess.run([str(t/'test'),*(['benchmark'] if args.benchmark else [])],check=True)
