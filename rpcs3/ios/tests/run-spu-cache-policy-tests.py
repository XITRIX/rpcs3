#!/usr/bin/env python3
"""Execute cache selection and runtime interpreter bootstrap in their real order."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=HERE.parents[1] / 'Emu/Cell/SPUCommonRecompiler.cpp')
parser.add_argument('--negative-control', action='store_true')
args = parser.parse_args()
source = args.source.read_text()
begin = source.index('\tconst bool spu_precompilation_enabled =')
end = source.index('\n\tu32 worker_count = 0;', begin)
body = source[begin:end]
if args.negative_control and 'func_list.clear();' in body:
    body = body.replace('func_list.clear();\n\t\ttotal_precompile = 0;\n\t\tdata_list = {};', 'return;', 1)
fixture = r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>
using usz=std::size_t;
enum class spu_decoder_type { _static, dynamic, llvm };
struct { struct { bool spu_cache=true,llvm_precompilation=true; spu_decoder_type spu_decoder=spu_decoder_type::llvm; } core; } g_cfg;
struct logger {
    template<class... T> void notice(const char*,T...){}
    template<class... T> void success(const char*,T...){}
    template<class... T> void fatal(const char*,T...){ std::abort(); }
} spu_log;
template<class T> struct atomic_t { atomic_t(T){} ~atomic_t(){} };
namespace spu_runtime { bool g_interpreter=false; }
unsigned interpreter_builds=0;
struct compiler_fixture {
    void init(){}
    bool compile(std::initializer_list<int>) {
        ++interpreter_builds; spu_runtime::g_interpreter=true; return true;
    }
};
struct spu_recompiler_base {
    static auto make_llvm_recompiler(int) { return std::make_unique<compiler_fixture>(); }
};
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
    g_cfg.core.spu_decoder=spu_decoder_type::dynamic;
    for (auto count : {20u, 0u}) {
        interpreter_builds=0; spu_runtime::g_interpreter=false;
        initialize(true,false,count);
        if (interpreter_builds!=1 || !spu_runtime::g_interpreter) {
            std::fputs("Cache-off skipped required dynamic interpreter bootstrap\n",stderr);
            return 86;
        }
        assert(compiled==0 && !cache_state.installed);
    }
    g_cfg.core.spu_decoder=spu_decoder_type::llvm;
    interpreter_builds=0;
    initialize(true,false,20); assert(compiled==0 && !cache_state.installed);
    initialize(true,false,0); assert(compiled==0 && !cache_state.installed);
    assert(interpreter_builds==2);
    g_cfg.core.spu_decoder=spu_decoder_type::_static;
    interpreter_builds=0; initialize(true,false,20); assert(compiled==0 && interpreter_builds==0);
    g_cfg.core.spu_decoder=spu_decoder_type::llvm;
    initialize(true,true,20); assert(compiled==20); // Deliberate debug override.
    compiled=0; g_cfg.core.spu_cache=true;
    initialize(false,false,20); assert(compiled==0);
    initialize(true,false,20); assert(compiled==20); // Warm-cache precompile remains.
    compiled=0; initialize(true,false,0); assert(compiled==4 && cache_state.installed);
    std::puts("SPU cache: warm/empty cache-off preserves dynamic/LLVM bootstrap; static, debug and cache-on paths passed.");
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
    result = subprocess.run([str(exe)])
    if args.negative_control:
        assert result.returncode == 86, f'Expected missing bootstrap, got {result.returncode}'
        print('Original early-return control rejected: cache-off bypasses runtime interpreter bootstrap.')
    else:
        result.check_returncode()
