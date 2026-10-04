#!/usr/bin/env python3
"""Execute production RemoteIO controls with deterministic Audio Unit stand-ins."""
from pathlib import Path
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
SOURCE = HERE.parent.parent
backend = (SOURCE / "Emu/Audio/IOS/IOSAudioBackend.cpp").read_text()


def method(name):
    start = backend.index(name)
    body = backend.index("{", start)
    depth = 1
    end = body + 1
    while depth:
        depth += (backend[end] == "{") - (backend[end] == "}")
        end += 1
    return backend[start:end]


methods = "\n\n".join(method(name) for name in (
    "bool IOSAudioBackend::IsPlaying()", "void IOSAudioBackend::Play()",
    "void IOSAudioBackend::Pause()", "void IOSAudioBackend::set_session_active(bool active)",
    "void IOSAudioBackend::Close()", "void IOSAudioBackend::close_unlocked()",
))
fixture = (HERE / "IOSAudioSessionTests.cpp").read_text().replace("/* PRODUCTION_METHODS */", methods)
with tempfile.TemporaryDirectory(prefix="rpcs3-remoteio-session-") as directory:
    path = Path(directory)
    source = path / "test.cpp"
    source.write_text(fixture)
    for sanitizer in (False, True):
        binary = path / ("test-sanitized" if sanitizer else "test")
        command = [os.environ.get("CXX", "clang++"), "-std=c++20", "-O1", "-g",
                   "-Wall", "-Wextra", "-Werror", "-pthread", "-I", str(SOURCE), str(source), "-o", str(binary)]
        if sanitizer:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True, timeout=15)

    # Restoring the missing output-unit restart must fail the direct-XMB case.
    broken = fixture.replace('AudioOutputUnitStart(m_unit), "AudioOutputUnitStart(session resume)"',
                             '0, "AudioOutputUnitStart(session resume)"')
    assert broken != fixture
    source.write_text(broken)
    binary = path / "negative-control"
    subprocess.run([os.environ.get("CXX", "clang++"), "-std=c++20", "-O1", "-pthread",
                    "-I", str(SOURCE), str(source), "-o", str(binary)], check=True)
    result = subprocess.run([str(binary)], capture_output=True, timeout=15)
    assert result.returncode != 0, "Missing RemoteIO restart was not detected"
    assert b"unit.running && unit.starts == 1" in result.stderr, result.stderr
    print("Missing RemoteIO restart negative control failed at the expected assertion")
