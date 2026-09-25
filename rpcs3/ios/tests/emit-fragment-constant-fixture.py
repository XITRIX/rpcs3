"""Extract both architecture branches of the production RSX predicate."""
from pathlib import Path
import sys
source = Path(__file__).resolve().parents[2] / 'Emu/RSX/Program/ProgramStateCache.cpp'
body = source.read_text().split('bool fragment_program_utils::is_any_src_constant(v128 sourceOperand)', 1)[1].split('\n}', 1)[0]
arm, old = body.split('#ifdef ARCH_ARM64', 1)[1].split('#else', 1)
old = old.split('#endif', 1)[0]
output = Path(sys.argv[1])
output.write_text('inline bool old_test(v128 sourceOperand) {' + old + '}\ninline bool new_test(v128 sourceOperand) {' + arm + '}\n')
