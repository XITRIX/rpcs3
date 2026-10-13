#!/usr/bin/env python3
"""Execute constant-table folding using the production helper and native LLVM.

Requires LLVM_DIR (22+) and a Darwin ARM64 host. All supported test masks execute against a scalar byte oracle; --sanitize
checks the harness (the emitted LLVM machine code is not instrumented).
Component timings are host observations, not predictions of game performance.
"""
import os
import platform
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
CORE = HERE.parents[1]


def main():
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        raise SystemExit('Darwin ARM64 host required; test was not run.')
    llvm = os.environ.get('LLVM_DIR')
    if not llvm:
        raise SystemExit('Set LLVM_DIR to the native LLVM lib/cmake/llvm directory.')
    out = Path(os.environ.get('SPU_TABLE_TEST_OUTPUT', '/tmp/rpcs3-spu-table-tests')).resolve()
    out.mkdir(parents=True, exist_ok=True)
    # CMake paths are quoted independently of the shell; subprocess uses argv.
    def quote(path):
        return '"' + str(path).replace('\\', '/').replace('"', '\\"') + '"'
    out.joinpath('CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.20)
project(SPUConstantTableTests C CXX)
set(CMAKE_CXX_STANDARD 20)
find_package(LLVM REQUIRED CONFIG)
if(LLVM_VERSION_MAJOR LESS 22)
  message(FATAL_ERROR "LLVM 22+ required")
endif()
add_executable(fixture ''' + quote(HERE / 'SPUARM64ConstantTableFixture.cpp') + ''')
target_include_directories(fixture PRIVATE ${LLVM_INCLUDE_DIRS} ''' + quote(CORE) + ''')
target_link_libraries(fixture PRIVATE LLVMTransformUtils LLVMAnalysis LLVMCore LLVMSupport LLVMTarget LLVMAArch64CodeGen LLVMAArch64AsmParser)
# Cross-built native archive packages may retain Linux host link metadata.
# Darwin supplies clock operations in libSystem, not a separate librt.
get_target_property(deps LLVMSupport INTERFACE_LINK_LIBRARIES)
list(REMOVE_ITEM deps rt)
set_target_properties(LLVMSupport PROPERTIES INTERFACE_LINK_LIBRARIES "${deps}")
''')
    def run(*args):
        subprocess.run([str(x) for x in args], check=True)
    run('cmake', '-S', out, '-B', out / 'build', '-G', 'Ninja', '-DLLVM_DIR=' + llvm)
    run('cmake', '--build', out / 'build')
    run(out / 'build/fixture', out / 'table')
    flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if '--sanitize' in sys.argv else []
    run('xcrun', 'clang++', '-O3', '-std=c++20', '-mcpu=apple-m1', *flags,
        HERE / 'SPUARM64ConstantTableTests.cpp', out / 'table.o', '-o', out / 'tests')
    run(out / 'tests', *(['validate'] if '--sanitize' in sys.argv or '--validate-only' in sys.argv else []))


if __name__ == '__main__':
    main()
