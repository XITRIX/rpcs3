"""Run the production ARM64 lowering and scalar oracles with host LLVM.

LLVM_CONFIG must name a native LLVM development installation (20 or newer).
Device LLVM archives cannot execute on the host. --bench adds alternating timing
rounds; these component timings are not predictions of device frame rate.
"""
from pathlib import Path
import os
import platform
import shlex
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parent
CORE = HERE.parents[1]


def main():
    config = os.environ.get('LLVM_CONFIG') or shutil.which('llvm-config')
    if not config:
        raise SystemExit('Set LLVM_CONFIG to a native LLVM 20+ llvm-config; this test was not run.')
    if platform.machine() not in ('arm64', 'aarch64'):
        raise SystemExit('ARM64 host required to execute the NEON fixtures.')
    out = Path(os.environ.get('SPU_COMPARE_TEST_OUTPUT', os.environ.get('TMPDIR', '/tmp') + '/rpcs3-spu-compare-tests'))
    out.mkdir(parents=True, exist_ok=True)
    def llvm(*args):
        return subprocess.check_output([config, *args], text=True).strip()
    def run(args):
        subprocess.run([str(x) for x in args], check=True)
    print('Host LLVM', llvm('--version'), 'host', platform.machine(), flush=True)
    if int(llvm('--version').split('.')[0]) < 20:
        raise SystemExit('LLVM 20+ required.')
    compiler = shlex.split(os.environ.get('CXX', 'clang++'))
    fixture = out / 'transform'
    run([*compiler, *shlex.split(llvm('--cxxflags')), '-std=c++20', '-O2', '-I', CORE,
         HERE / 'SPUARM64CompareFixture.cpp', *shlex.split(llvm('--ldflags', '--libs', 'all', '--system-libs')),
         '-Wl,-rpath,'+llvm('--libdir'), '-o', fixture])
    run([sys.executable, HERE / 'emit-spu-compare-fixture.py', out / 'compare.ll'])
    run([fixture, out / 'compare.ll', out / 'compare-pipeline.ll', 'pipeline'])
    target = 'arm64-apple-macos15' if platform.system() == 'Darwin' else 'aarch64-unknown-linux-gnu'
    cpu = os.environ.get('SPU_COMPARE_TEST_CPU', 'generic')
    run([Path(llvm('--bindir')) / 'llc', '-O2', '-mtriple='+target, '-mcpu='+cpu, '-filetype=obj',
         out / 'compare-pipeline.ll', '-o', out / 'compare.o'])
    run([*compiler, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-iquote', out,
         HERE / 'SPUARM64CompareTests.cpp', out / 'compare.o', '-o', out / 'compare'])
    run([out / 'compare', *sys.argv[1:]])


if __name__ == '__main__':
    main()
