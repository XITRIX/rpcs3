#!/usr/bin/env python3
"""Exercise production cfg parsing/serialization and the port's shader option filter."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--shader-header', type=Path, default=ROOT / 'rpcs3/ios/RPCS3IOSShaderMode.h')
args = parser.parse_args()

with tempfile.TemporaryDirectory(prefix='rpcs3-shader-settings-') as directory:
    out = Path(directory)
    header = out / 'ios/RPCS3IOSShaderMode.h'
    header.parent.mkdir()
    header.write_text(args.shader_header.read_text())
    source = (ROOT / 'rpcs3/util/logs.cpp').read_text()
    start = source.index('template<>\nvoid fmt_class_string<logs::level>::format')
    end = source.index('\nnamespace logs', start)
    (out / 'LogLevelFormatter.inc').write_text(source[start:end])
    source = (ROOT / 'rpcs3/ios/RPCS3IOS.cpp').read_text()
    start = source.index('std::erase_if(options, [&setting]')
    end = source.index('\n\t\t\t});', start) + len('\n\t\t\t});')
    (out / 'SettingsOptionFilter.inc').write_text(source[start:end])

    yaml = ROOT / '3rdparty/yaml-cpp/yaml-cpp'
    flags = ['-std=c++20', '-O1', '-Wno-deprecated-declarations', '-ffunction-sections', '-fdata-sections']
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    includes = [out, ROOT, ROOT / 'rpcs3', ROOT / 'rpcs3/ios', yaml / 'include',
                ROOT / '3rdparty/asmjit/asmjit/src']
    for path in includes:
        flags += ['-I', str(path)]
    compiler = os.environ.get('CXX', 'clang++')
    sources = [ROOT / 'Utilities/Config.cpp', ROOT / 'Utilities/StrFmt.cpp',
               ROOT / 'rpcs3/Emu/system_config_types.cpp', ROOT / 'rpcs3/util/yaml.cpp']
    sources += sorted((yaml / 'src').glob('*.cpp'))

    def compile_source(item):
        index, source = item
        target = out / f'{index}.o'
        subprocess.run([compiler, *flags, '-c', str(source), '-o', str(target)], check=True)
        return str(target)

    with ThreadPoolExecutor(max_workers=min(8, os.cpu_count() or 1)) as pool:
        objects = list(pool.map(compile_source, enumerate(sources)))
    linker_flags = ['-Wl,-dead_strip'] if sys.platform == 'darwin' else ['-Wl,--gc-sections']
    for platform in ('ios', 'desktop'):
        executable = out / f'ShaderModeMigrationTests-{platform}'
        command = [compiler, *flags, *linker_flags]
        if platform == 'ios':
            command += ['-DRPCS3_IOS']
        command += [str(HERE / 'ShaderModeMigrationTests.cpp'), *objects, '-o', str(executable)]
        subprocess.run(command, check=True)
        print(platform, flush=True)
        subprocess.run([str(executable)], check=True)
