#!/usr/bin/env python3
"""Execute startup cache selection with warm lists and explicit debug overrides."""
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
source = (HERE.parents[1] / 'Emu/Cell/SPUCommonRecompiler.cpp').read_text()
begin = source.index('\tconst bool spu_precompilation_enabled =')
end = source.index('\n\tatomic_t<usz> data_indexer', begin)
body = source[begin:end]
fixture = r'''
#include <cassert>
#include <cstdio>
#include <utility>
#include <vector>
using usz=std::size_t;
struct { struct { bool spu_cache=true,llvm_precompilation=true; } core; } g_cfg;
struct { template<class... T> void notice(const char*,T...){} } spu_log;
struct spu_cache { bool installed=false; } cache_state;
struct fxo { template<class T> T& get(){return cache_state;} } storage;
auto* g_fxo=&storage;
unsigned compiled=0;
void initialize(bool build_existing_cache,bool is_debug,unsigned count) {
    std::vector<int> func_list(count),data_list(4);
    spu_cache cache{true}; usz total_precompile=4;
    const char* loc="temporary-cache";
    @BODY@
    compiled+=func_list.size()+total_precompile;
}
int main() {
    g_cfg.core.spu_cache=false;
    initialize(true,false,20); assert(compiled==0 && !cache_state.installed);
    initialize(true,false,0); assert(compiled==0 && !cache_state.installed);
    initialize(true,true,20); assert(compiled==20); // Deliberate debug override.
    compiled=0; g_cfg.core.spu_cache=true;
    initialize(false,false,20); assert(compiled==0);
    initialize(true,false,20); assert(compiled==20); // Warm-cache precompile remains.
    compiled=0; initialize(true,false,0); assert(compiled==4 && cache_state.installed);
    std::puts("SPU cache: disabled warm/empty lists, debug override and enabled startup paths passed.");
}
'''.replace('@BODY@', body)
with tempfile.TemporaryDirectory(prefix='spu-cache-policy-') as directory:
    temp = Path(directory)
    test = temp / 'tests.cpp'
    test.write_text(fixture)
    exe = temp / 'tests'
    subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++20', '-O2',
                    '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    str(test), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
