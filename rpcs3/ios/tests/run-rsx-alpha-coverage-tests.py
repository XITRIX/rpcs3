#!/usr/bin/env python3
"""Execute production A2C selection, invalidation and ROP ordering with C++ stubs."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
CORE = Path(os.environ.get("RPCS3_TEST_SOURCE_ROOT", HERE.parents[2]))
RSX = CORE / "rpcs3/Emu/RSX"
parser = argparse.ArgumentParser()
parser.add_argument("--sanitize", action="store_true")
parser.add_argument("--negative-control", action="store_true")
args = parser.parse_args()


def block(source, marker):
    start = source.index(marker)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


selection = block((RSX / "RSXThread.cpp").read_text(),
                  "if (REGS(m_ctx)->msaa_alpha_to_coverage_enabled())")
invalidation = block((RSX / "NV47/HW/nv4097.cpp").read_text(), "void set_aa_control(")
enums = (RSX / "gcm_enums.h").read_text()
flags = "\n".join("constexpr u32 " + re.search(
    rf"{name}\s*=\s*0x[0-9a-fA-F]+", enums).group() + ";"
    for name in ("RSX_SHADER_CONTROL_ALPHA_TO_COVERAGE", "RSX_SHADER_CONTROL_ALPHA_TO_ONE"))

# Ensure both frontend consumers connect the program key to the shared epilogue.
for frontend in ("GL/GLFragmentProgram.cpp", "VK/VKFragmentProgram.cpp"):
    assert "m_shader_props.ROP_alpha_to_one = !!(m_prog.ctrl & RSX_SHADER_CONTROL_ALPHA_TO_ONE);" in (RSX / frontend).read_text()
switch = block((RSX / "Program/GLSLCommon.cpp").read_text(), "if (props.ROP_alpha_to_one)")
assert 'enabled_options.push_back("_ENABLE_ALPHA_TO_ONE")' in switch
epilogue = (RSX / "Program/GLSLSnippets/RSXProg/RSXROPEpilogue.glsl").read_text()
epilogue = epilogue[epilogue.index('R"(') + 3:epilogue.rindex(')"')]
if args.negative_control:
    epilogue = epilogue.replace("col0.a = _mrt_color_t(vec4(1.)).a;", "/* omitted alpha-to-one */")

with tempfile.TemporaryDirectory(prefix="rsx-alpha-coverage-") as directory:
    target = Path(directory)
    (target / "selection.inc").write_text(selection)
    (target / "invalidation.inc").write_text(invalidation)
    (target / "flags.inc").write_text(flags)
    (target / "rop.inc").write_text(epilogue)
    executable = target / "test"
    command = [os.environ.get("CXX", "clang++"), "-std=c++20", "-O2", "-I", str(target),
               str(HERE / "RSXAlphaCoverageTests.cpp"), "-o", str(executable)]
    if args.sanitize:
        command[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command, check=True)
    result = subprocess.run([str(executable)])
    if args.negative_control:
        assert result.returncode == 1, "missing alpha-to-one must fail executed tests"
        print("Negative control failed as expected.")
    else:
        result.check_returncode()
