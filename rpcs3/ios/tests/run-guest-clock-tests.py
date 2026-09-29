#!/usr/bin/env python3
"""Execute production guest clocks and LV2 waits with a deterministic host clock."""
import argparse
import os
import re
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--clock-header', type=Path, default=ROOT / 'rpcs3/Emu/Cell/GuestClock.h')
parser.add_argument('--time-source', type=Path, default=ROOT / 'rpcs3/Emu/Cell/lv2/sys_time.cpp')
parser.add_argument('--wait-source', type=Path, default=ROOT / 'rpcs3/Emu/Cell/lv2/lv2.cpp')
parser.add_argument('--pipeline-source', type=Path, default=ROOT / 'rpcs3/Emu/RSX/VK/VKPipelineCompiler.cpp')
parser.add_argument('--shader-source', type=Path, default=ROOT / 'rpcs3/Emu/RSX/Program/ProgramStateCache.h')
parser.add_argument('--spu-source', type=Path, default=ROOT / 'rpcs3/Emu/Cell/SPUCommonRecompiler.cpp')
parser.add_argument('--ppu-source', type=Path, default=ROOT / 'rpcs3/Emu/Cell/PPUThread.cpp')
args = parser.parse_args()


def method(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


with tempfile.TemporaryDirectory(prefix='rpcs3-guest-clock-') as directory:
    out = Path(directory)
    clock_header = out / 'Emu/Cell/GuestClock.h'
    clock_header.parent.mkdir(parents=True)
    clock_header.write_text(args.clock_header.read_text())
    source = args.time_source.read_text()
    # Old-source controls retain the new lifecycle entry points so each missing
    # integration (clock mapping or wait accounting) can fail independently.
    current = (ROOT / 'rpcs3/Emu/Cell/lv2/sys_time.cpp').read_text()
    signatures = ('u64 get_active_system_time(', 'void pause_guest_time(', 'void resume_guest_time(',
                  'u64 begin_guest_time_stall(', 'void end_guest_time_stall(',
                  'u64 convert_to_timebased_time(', 'u64 get_timebased_time(',
                  'void initialize_timebased_time(', 'u64 get_guest_system_time(')
    (out / 'GuestTime.inc').write_text('\n'.join(method(source if s in source else current, s) for s in signatures))
    (out / 'GuestWait.inc').write_text(method(args.wait_source.read_text(), 'bool lv2_obj::wait_timeout('))
    audio = (ROOT / 'rpcs3/Emu/Cell/Modules/cellAudio.cpp').read_text()
    advance = method(audio, 'void cell_audio_thread::advance(')
    query = method(audio, 'error_code cellAudioGetPortTimestamp(')
    (out / 'AudioTimestampCapture.inc').write_text(re.search(r'guest_timestamps\[port.number\] = [^;]+;', advance)[0])
    (out / 'AudioTimestampQuery.inc').write_text(re.search(r'\*stamp = [^;]+;', query)[0])
    (out / 'AudioTimestampRestore.inc').write_text(method(audio, 'u64 cell_audio_thread::get_port_guest_timestamp('))
    pipeline = args.pipeline_source.read_text()
    signature = 'std::unique_ptr<glsl::program> pipe_compiler::compile('
    definitions = []
    while signature in pipeline:
        definition = method(pipeline, signature)
        definitions.append(definition)
        pipeline = pipeline[pipeline.index(definition) + len(definition):]
    assert len(definitions) == 4
    (out / 'StallPipelineDefinitions.inc').write_text('\n'.join(definitions))
    (out / 'StallPipelineDeclarations.inc').write_text('\n'.join(
        d[:d.index('{')].replace('pipe_compiler::', '') + ';' for d in definitions))
    shader = args.shader_source.read_text()
    (out / 'StallShaderMethods.inc').write_text('\n'.join(method(shader, signature) for signature in (
        'std::tuple<const vertex_program_type&, bool> search_vertex_program(',
        'std::tuple<const fragment_program_type&, bool> search_fragment_program(')))
    spu = args.spu_source.read_text()
    start = spu.index('\tspu_program program;', spu.index('void spu_recompiler_base::dispatch('))
    scope = method(spu[start:], '\tspu_program program;')
    assert 'g_escape' not in scope and 'g_tail_escape' not in scope
    (out / 'StallSPUCompile.inc').write_text(scope.replace('#ifdef ARCH_ARM64', '#if 1'))
    ppu = method(args.ppu_source.read_text(), 'bool ppu_initialize(const ppu_module<lv2_obj>& info, bool check_only, u64 file_size)\n')
    hold = re.search(r'const guest_time_stall clock_hold\([^;]+;', ppu)
    assert hold and ppu.index(hold[0]) < ppu.index('for (auto& func : info.get_funcs())', ppu.index('const bool is_being_used_in_emulation'))
    assert ppu.index(hold[0]) < ppu.index('threads.join()')
    (out / 'StallPPUHold.inc').write_text(hold[0])
    command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-O2', '-pthread',
               '-Wall', '-Wextra', '-Werror', '-I', str(out), '-I', str(ROOT / 'rpcs3'), '-I', str(ROOT),
               str(HERE / 'GuestClockTests.cpp'), '-o', str(out / 'test')]
    if args.sanitize:
        command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(command, check=True)
    subprocess.run([str(out / 'test')], check=True, timeout=30)
