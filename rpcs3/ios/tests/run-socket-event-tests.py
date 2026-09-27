#!/usr/bin/env python3
"""Exercise production socket dispatch, including real loopback connect completion."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
NET = HERE.parents[1] / "Emu/Cell/lv2/sys_net"
parser = argparse.ArgumentParser()
parser.add_argument("--sanitize", action="store_true")
parser.add_argument("--sockets", action="store_true", help="also exercise host loopback sockets")
parser.add_argument("--source", type=Path, default=NET / "lv2_socket.cpp")
args = parser.parse_args()


def method(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


with tempfile.TemporaryDirectory(prefix="rpcs3-socket-events-") as directory:
    root = Path(directory)
    (root / "SocketEvents.inc").write_text(method(args.source.read_text(), "void lv2_socket::handle_events("))
    (root / "SocketConnect.inc").write_text(method((NET / "lv2_socket_native.cpp").read_text(), "s32 lv2_socket_native::connect_followup("))
    executable = root / "socket-tests"
    command = [os.environ.get("CXX", "clang++"), "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
               "-I", str(root), str(HERE / "SocketEventTests.cpp"), "-o", str(executable)]
    if args.sanitize:
        command[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command, check=True)
    subprocess.run([str(executable)] + (["--sockets"] if args.sockets else []), check=True, timeout=20)
