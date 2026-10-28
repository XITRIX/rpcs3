#!/usr/bin/env python3
"""Run production GDB packet IO/accept loop against host sockets."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, default=ROOT / 'rpcs3/Emu/GDB.cpp')
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
source = args.source.read_text()
names = ['read', 'read_char', 'read_hexbyte', 'try_read_cmd', 'read_cmd',
         'send', 'send_char', 'ack', 'send_cmd', 'send_cmd_ack',
         'append_encoded_char', 'to_hexbyte', 'wait_with_interrupts', 'cmd_supported', 'operator()']
methods = []
declarations = []
for name in names:
    marker = 'gdb_thread::' + name + '('
    start = source.rfind('\n', 0, source.index(marker)) + 1
    end = source.index('\n}\n', start) + 3
    method = source[start:end]
    methods.append(method)
    declaration = method.split('\n', 1)[0].replace('gdb_thread::', '')
    if name in ['append_encoded_char', 'to_hexbyte']:
        declaration = 'static ' + declaration
    declarations.append(declaration + ';')
handlers = ['cmd_extended_mode', 'cmd_reason', 'cmd_thread_info', 'cmd_current_thread',
            'cmd_read_register', 'cmd_write_register', 'cmd_read_memory', 'cmd_write_memory',
            'cmd_read_all_registers', 'cmd_write_all_registers', 'cmd_set_thread_ops',
            'cmd_attached_to_what', 'cmd_kill', 'cmd_continue_support', 'cmd_vcont',
            'cmd_remove_breakpoint', 'cmd_set_breakpoint']
class_source = '''class gdb_thread { public:
int server_socket = -1, client_socket = -1;
bool paused = false;
void start_server() {}
''' + '\n'.join(declarations) + '\n' + '\n'.join(
    'bool ' + name + '(const gdb_cmd&) { return false; }' for name in handlers) + '\n};\n'
with tempfile.TemporaryDirectory(prefix='gdb-disconnect-') as directory:
    out = Path(directory)
    (out / 'GDBClassUnderTest.inc').write_text(class_source)
    (out / 'GDBMethodsUnderTest.inc').write_text('\n\n'.join(methods))
    command = [*shlex.split(os.environ.get('CXX', 'clang++')), '-std=c++20', '-O2', '-g',
               '-Wall', '-Wextra', '-Werror', '-pthread', '-iquote', str(out),
               str(HERE / 'GDBDisconnectTests.cpp'), '-o', str(out / 'tests')]
    if args.sanitize:
        command[1:1] = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all']
    subprocess.run(command, check=True)
    subprocess.run([str(out / 'tests')], check=True, timeout=20)
