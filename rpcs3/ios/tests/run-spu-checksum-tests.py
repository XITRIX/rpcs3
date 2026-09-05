#!/usr/bin/env python3
"""Execute the production ARM64 checksum emitter using a host LLVM installation.

Example: python3 run-spu-checksum-tests.py --llvm-config /path/to/llvm-config
Requires an ARM64 host, clang++, and LLVM 20+ development headers/tools.
The ordinary contract runner does not require a host LLVM installation.
"""

import argparse
import os
import pathlib
import platform
import shlex
import shutil
import subprocess
import tempfile


def run(arguments, **kwargs):
    return subprocess.run([str(arg) for arg in arguments], check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--llvm-config", default=os.environ.get("LLVM_CONFIG", shutil.which("llvm-config")))
    parser.add_argument("--output-dir", type=pathlib.Path)
    parser.add_argument("--benchmark", action="store_true")
    parser.add_argument("--interrupts", action="store_true", help="test the generated interrupt fast path instead of checksum reduction")
    args = parser.parse_args()
    if platform.machine() not in ("arm64", "aarch64"):
        parser.error("generated-code execution requires an ARM64 host")
    if not args.llvm_config:
        parser.error("provide --llvm-config or set LLVM_CONFIG to a host LLVM 20+ installation")

    def llvm_config(*flags):
        return run([args.llvm_config, *flags], capture_output=True, text=True).stdout.strip()

    version = llvm_config("--version")
    if int(version.split(".")[0]) < 20:
        parser.error("LLVM 20 or newer is required")

    llc = pathlib.Path(llvm_config("--bindir")) / "llc"
    cxx = os.environ.get("CXX", "clang++")
    sources = pathlib.Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix="rpcs3-spu-checksum-") as temporary:
        output = (args.output_dir or pathlib.Path(temporary)).resolve()
        output.mkdir(parents=True, exist_ok=True)
        probe = "SPUInterrupt" if args.interrupts else "SPUChecksum"
        generator = output / f"{probe}Codegen"
        runner = output / f"{probe}Tests"
        ir = output / "checksum.ll"
        assembly = output / "checksum.s"
        obj = output / "checksum.o"
        flags = shlex.split(llvm_config("--cxxflags", "--ldflags", "--libs", "core", "--system-libs"))
        run([cxx, *flags, "-std=c++20", "-O2", "-I", sources.parents[1],
             sources / f"{probe}Codegen.cpp", "-o", generator])
        with ir.open("w") as stream:
            run([generator], stdout=stream)

        if platform.system() == "Darwin":
            target = ["-mtriple=arm64-apple-macosx14.0.0", "-mcpu=apple-m1"]
        else:
            target = ["-mtriple=aarch64-unknown-linux-gnu", "-mcpu=generic", "-mattr=+neon"]
        run([llc, "-O2", *target, ir, "-o", assembly])
        run([llc, "-O2", *target, "-filetype=obj", ir, "-o", obj])
        run([cxx, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
             sources / f"{probe}Tests.cpp", obj, "-o", runner])
        print(f"Executing production {probe} emitter with host LLVM {version}", flush=True)
        run([runner, *(["--benchmark"] if args.benchmark else [])])
        if args.output_dir:
            print(f"Generated IR, assembly, object, and executables: {output}")


if __name__ == "__main__":
    main()
