#!/usr/bin/env python3
from pathlib import Path
import argparse,subprocess,tempfile,os
HERE=Path(__file__).resolve().parent;CORE=HERE.parents[2]
p=argparse.ArgumentParser();p.add_argument('--candidate-root',type=Path,default=CORE);p.add_argument('--benchmark',action='store_true');p.add_argument('--sanitize',action='store_true');args=p.parse_args()
with tempfile.TemporaryDirectory(prefix='minecraft-dma-') as d:
 t=Path(d);(t/'dma-original.h').write_text((HERE/'MinecraftReference/IOSDMACopy.h').read_text().replace('rpcs3::ios','baseline::ios'))
 common=[os.environ.get('CXX','clang++'),'-std=c++20','-O2','-Wall','-Wextra','-Werror','-I',str(t),'-I',str(args.candidate_root/'rpcs3'),'-I',str(CORE),'-I',str(CORE/'rpcs3')]
 if args.sanitize:common[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
 subprocess.run([*common,str(HERE/'IOSFPSBatchTests.cpp'),str(CORE/'rpcs3/ios/IOSTextureHash.cpp'),'-o',str(t/'test')],check=True)
 subprocess.run([str(t/'test')],check=True)
 if args.benchmark:
  subprocess.run([*common,str(HERE/'MinecraftDMABenchmark.cpp'),'-o',str(t/'bench')],check=True);subprocess.run([str(t/'bench')],check=True)
