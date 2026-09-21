#!/usr/bin/env python3
"""Exercise production logging gates and silence/reset with a recording transport."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=ROOT / 'rpcs3/util/logs.cpp')
args = parser.parse_args()
source = args.source.read_text()
start = source.index('\tvoid reset()\n')
end = source.index('\n\tvoid set_level(', start)
with tempfile.TemporaryDirectory(prefix='rpcs3-silenced-fatal-') as directory:
    temp = Path(directory)
    (temp / 'LoggingControlsUnderTest.inc').write_text(source[start:end])
    for platform in ('ios', 'desktop'):
        executable = temp / ('SilencedFatalLogTests-' + platform)
        command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                   '-I', str(ROOT), '-I', str(ROOT / 'rpcs3'), '-I', str(temp),
                   str(HERE / 'SilencedFatalLogTests.cpp'), '-o', str(executable)]
        if platform == 'ios':
            command.append('-DRPCS3_IOS')
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True)
