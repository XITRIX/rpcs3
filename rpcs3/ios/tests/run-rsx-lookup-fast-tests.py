#!/usr/bin/env python3
"""Production fragment comparison and FIFO traversal tests."""
from pathlib import Path
import argparse,os,re,subprocess,tempfile
HERE=Path(__file__).resolve().parent;CORE=HERE.parents[2]
CORE = Path(os.environ.get("RPCS3_TEST_SOURCE_ROOT", CORE))
p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');p.add_argument('--benchmark',action='store_true');p.add_argument('--shader-dir',type=Path);args=p.parse_args()
def block(s,marker):
    a=s.index(marker);b=s.index('{',a)+1;d=1
    while d:d+=(s[b]=='{')-(s[b]=='}');b+=1
    return s[a:b]
with tempfile.TemporaryDirectory(prefix='rsx-lookup-fast-') as temp:
    t=Path(temp)
    for tag in ['old','new']:
        ref=HERE/'GTA5SyncReference';vk=CORE/'rpcs3/Emu/RSX/VK/vkutils'
        src=(ref/'Fragment.inc').read_text() if tag=='old' else (CORE/'rpcs3/Emu/RSX/Program/ProgramStateCache.cpp').read_text()
        fragments='\n'.join(block(src,m) for m in ['bool fragment_program_utils::is_any_src_constant(','bool fragment_program_compare::compare_properties(','bool fragment_program_compare::operator()('])
        (t/(tag+'-fragment.inc')).write_text(fragments)
        src=(ref/'FIFO.inc').read_text() if tag=='old' else (CORE/'rpcs3/Emu/RSX/RSXFIFO.cpp').read_text()
        fifo=block(src,'const auto next_cache_line =').split('{',1)[1]
        (t/(tag+'-fifo.inc')).write_text('__attribute__((noinline)) unsigned fifo_'+tag+'(u32 to_fetch,int current,u32 m_cache_line_count) {'+fifo)
    exe=t/'test';cmd=[os.environ.get('CXX','clang++'),'-std=c++20','-O2','-Wall','-Wextra','-Werror','-I',str(t),'-I',str(CORE/'rpcs3'),'-I',str(CORE),str(HERE/'RSXLookupFastTests.cpp'),'-o',str(exe)]
    if args.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run(cmd,check=True)
    command=[str(exe)]+(['benchmark'] if args.benchmark else ['validate'])
    if args.shader_dir:command += [str(x) for x in sorted(args.shader_dir.glob('*.bin'))]
    subprocess.run(command,check=True)
