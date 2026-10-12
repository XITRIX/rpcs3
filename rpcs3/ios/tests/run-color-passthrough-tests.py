#!/usr/bin/env python3
"""Exercise the production decoder used to preserve clear-like async draws."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--negative-control', action='store_true')
args = parser.parse_args()
source = (root / 'rpcs3/Emu/RSX/Program/RSXFragmentProgram.h').read_text()
unions = source[source.index('union OPDEST'):source.index('union SRC2')]
start = source.index('\tbool is_color_passthrough() const')
end = source.index('\n\tvoid clone_data()', start)
method = source[start:end]
if args.negative_control:
    method = 'bool is_color_passthrough() const { return false; }'
renderer = (root / 'rpcs3/Emu/RSX/VK/VKGSRender.cpp').read_text()
assert '(shadermode != shader_mode::async_recompiler || !fragment_program.is_color_passthrough()) &&' in renderer
fixture = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
using u32 = std::uint32_t;
enum { RSX_FP_OPCODE_MOV = 1, RSX_FP_REGISTER_TYPE_INPUT = 1 };
#include "Unions.inc"
struct Program {
    u32 ucode_length = 16;
    struct { void* data_ptr; } data;
    void* get_data() const { return data.data_ptr; }
#include "Method.inc"
};
constexpr u32 encode(u32 x) { return ((x & 0x00ff00ff) << 8) | ((x & 0xff00ff00) >> 8); }
int main() {
    // The actual single-MOV program at GTA V frame 5 draw 2042.
    const std::array<u32,4> decoded{0x01403e81, 0x1c9dc801, 0x0001c800, 0x3fe1c800};
    std::array<u32,4> words;
    for (unsigned i=0;i<4;i++) words[i]=encode(decoded[i]);
    Program p{16,{words.data()}};
    assert(p.is_color_passthrough());
    p.ucode_length=32; assert(!p.is_color_passthrough()); p.ucode_length=16;
    p.data.data_ptr=nullptr; assert(!p.is_color_passthrough()); p.data.data_ptr=words.data();
    // End, opcode, destination, type, mask, input, condition and high opcode.
    for (u32 bit : {1u, 1u<<24, 1u<<1, 1u<<7, 1u<<9, 1u<<13, 1u<<30}) {
        words[0]=encode(decoded[0]^bit); assert(!p.is_color_passthrough());
    }
    words[0]=encode(decoded[0]);
    for (u32 bit : {1u, 1u<<18, 1u<<19, 1u<<20}) {
        words[1]=encode(decoded[1]^bit); assert(!p.is_color_passthrough());
    }
    words[1]=encode(decoded[1]); words[2]=encode(decoded[2]|(1u<<31));
    assert(!p.is_color_passthrough());
    std::cout << "Color-passthrough production decoder and negative cases pass\n";
}
'''
with tempfile.TemporaryDirectory(prefix='rsx-color-pass-') as directory:
    path = Path(directory)
    (path / 'Unions.inc').write_text(unions)
    (path / 'Method.inc').write_text(method)
    (path / 'test.cpp').write_text(fixture)
    exe = path / 'test'
    command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I', str(path), str(path / 'test.cpp'), '-o', str(exe)]
    if args.sanitize:
        command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(command, check=True)
    result = subprocess.run([str(exe)], capture_output=args.negative_control)
    if args.negative_control:
        assert result.returncode != 0, 'Original skip policy unexpectedly passed'
        assert b'Assertion' in result.stderr
        print('Negative control rejects the original async skip policy')
    else:
        result.check_returncode()
