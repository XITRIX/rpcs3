#!/usr/bin/env python3
"""Compile the production LLVM checkpoint emitter and execute optimized ARM64 checkpoints."""
import argparse,os,shlex,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=HERE.parents[1]/'Emu/Cell/SPULLVMRecompiler.cpp');a=p.parse_args();s=a.source.read_text()
def extract(signature):
    start=s.index(signature);opening=s.index('{',start);depth=1;end=opening+1
    while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[start:end]
code='\n'.join(extract(x) for x in ['\tvoid check_state(u32 addr,', '\tvoid ensure_gpr_stores()', '\tllvm::Value* double_to_xfloat('])
config=os.environ.get('LLVM_CONFIG','/opt/homebrew/opt/llvm@20/bin/llvm-config')
def llvm(*args):return subprocess.check_output([config,*args],text=True).strip()
def run(args,**kw):subprocess.run(list(map(str,args)),check=True,**kw)
with tempfile.TemporaryDirectory(prefix='spu-checkpoint-') as tmp:
    out=Path(tmp);(out/'production.inc').write_text(code)
    run([os.environ.get('CXX','clang++'),*shlex.split(llvm('--cxxflags')),'-std=c++20','-O2','-I',out,HERE/'SPUCheckpointEmitter.cpp',*shlex.split(llvm('--ldflags','--libs','all','--system-libs')),'-Wl,-rpath,'+llvm('--libdir'),'-o',out/'emit'])
    with (out/'checkpoint.ll').open('w') as f:run([out/'emit'],stdout=f)
    run([Path(llvm('--bindir'))/'llc','-O2','-mtriple=arm64-apple-macos15','-filetype=obj',out/'checkpoint.ll','-o',out/'checkpoint.o'])
    run([os.environ.get('CXX','clang++'),'-std=c++20','-O2',HERE/'SPUCheckpointTests.cpp',out/'checkpoint.o','-o',out/'tests'])
    run([out/'tests'])
