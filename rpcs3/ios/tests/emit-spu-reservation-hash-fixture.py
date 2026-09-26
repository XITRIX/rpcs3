"""Extract the unchanged portable hash body for an exact production comparison."""
from pathlib import Path
import sys

source = (Path(__file__).resolve().parents[2] / 'Emu/Cell/SPUThread.cpp').read_text()
body = source.split('extern u32 compute_rdata_hash32(', 1)[1].split('#else', 1)[1].split('#endif', 1)[0]
assert 'gv_add32' in body and 'return r1._u32[0] + r1._u32[2];' in body
Path(sys.argv[1]).write_text('__attribute__((noinline)) u32 baseline(const void* _src)\n{'+body+'}\n')
