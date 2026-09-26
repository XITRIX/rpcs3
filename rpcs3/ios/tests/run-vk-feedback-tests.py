#!/usr/bin/env python3
"""Execute production fragment analysis and feedback binding with fake GPU resources.

This validates selection, copy lifetime and ordering requests, not GPU execution.
Optional --shader-dir analyzes local captured shaders without distributing them.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--negative-control', action='store_true')
parser.add_argument('--shader-dir', type=Path)
args = parser.parse_args()
program = ROOT / 'rpcs3/Emu/RSX/Program'
vk = ROOT / 'rpcs3/Emu/RSX/VK'


def block(text, marker, suffix=''):
    start = text.index(marker)
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end] + suffix


header = (program / 'ProgramStateCache.h').read_text()
source = (program / 'ProgramStateCache.cpp').read_text()
draw = (vk / 'VKDraw.cpp').read_text()
analysis = block(source, 'bool fragment_program_utils::is_any_src_constant(') + '\n'
analysis += block(source, 'fragment_program_utils::fragment_program_metadata fragment_program_utils::analyse_fragment_program(')
if args.negative_control:
    analysis = analysis.replace(
        'result.multiple_texture_reads_mask |= result.referenced_textures_mask & (1u << d0.tex_num);', '')

# Both the compiled and interpreter paths must use the same binding correction.
for name in ('bind_texture_env', 'bind_interpreter_texture_env'):
    assert 'snapshot_color_feedback(i, view)' in block(draw, f'bool VKGSRender::{name}()')

with tempfile.TemporaryDirectory(prefix='rpcs3-vk-feedback-') as directory:
    temp = Path(directory)
    (temp / 'FragmentAnalysis.inc').write_text(
        'namespace rsx::assembler {\n' +
        block((program / 'Assembler/FPOpcodes.h').read_text(), 'enum FP_opcode', ';') + '\n}\n' +
        'using enum rsx::assembler::FP_opcode;\n' +
        block((program / 'RSXFragmentProgram.h').read_text(), 'union OPDEST', ';') + '\n' +
        'struct fragment_program_utils {\n' +
        block(header, 'struct fragment_program_metadata', ';') + '\n' +
        'static bool is_any_src_constant(v128);\n' +
        'static fragment_program_metadata analyse_fragment_program(const void*);\n};\n' + analysis)
    (temp / 'VKFeedback.inc').write_text(block(draw, 'vk::image_view* VKGSRender::snapshot_color_feedback('))
    executable = temp / 'VKFeedbackTests'
    command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I', str(temp), str(HERE / 'VKFeedbackTests.cpp'), '-o', str(executable)]
    if args.sanitize:
        command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(command, check=True)
    command = [str(executable)]
    if args.shader_dir:
        command += [str(p) for p in sorted(args.shader_dir.glob('*.bin'))]
    result = subprocess.run(command)
    if args.negative_control:
        if result.returncode != 1:
            raise SystemExit('Expected old repeated-read analysis to fail the regression')
        print('Old-analysis negative control failed as expected')
    else:
        result.check_returncode()
