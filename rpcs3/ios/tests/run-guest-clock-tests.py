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
parser.add_argument('--time-source', type=Path, default=ROOT / 'rpcs3/Emu/Cell/lv2/sys_time.cpp')
parser.add_argument('--wait-source', type=Path, default=ROOT / 'rpcs3/Emu/Cell/lv2/lv2.cpp')
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
    source = args.time_source.read_text()
    # Old-source controls retain the new lifecycle entry points so each missing
    # integration (clock mapping or wait accounting) can fail independently.
    current = (ROOT / 'rpcs3/Emu/Cell/lv2/sys_time.cpp').read_text()
    signatures = ('u64 get_active_system_time(', 'void pause_guest_time(', 'void resume_guest_time(',
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
    command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-O2', '-pthread',
               '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'rpcs3'), '-I', str(ROOT),
               '-I', str(out), str(HERE / 'GuestClockTests.cpp'), '-o', str(out / 'test')]
    if args.sanitize:
        command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(command, check=True)
    subprocess.run([str(out / 'test')], check=True, timeout=30)
