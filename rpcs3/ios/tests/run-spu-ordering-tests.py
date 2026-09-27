#!/usr/bin/env python3
"""Audit reservation ordering sites and verify the production fence emits ARM64 DMB ISHLD.

This is a source/code-generation contract, not a proof of weak-memory behavior on a device.
"""
import argparse,os,re,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent;CORE=HERE.parents[1]
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=CORE/'Emu/Cell/SPUThread.cpp');a=p.parse_args();s=a.source.read_text()
patterns=[
 r'mov_rdata\(rdata, data\);\s*atomic_fence_acquire\(\);\s*if \(u64 time0 = vm::reservation_acquire',
 r'atomic_fence_acquire\(\);\s*if \(time0 != vm::reservation_acquire\(eal\)',
 r'cmp_rdata\(rdata, vm::_ref<spu_rdata_t>\(addr\)\) && rdata_fence\(\) && res == rtime',
 r'cmp_rdata\(sdata, write_data\) && rdata_fence\(\) && at_read_time ==\s*vm::reservation_acquire',
 r'rdata_fence\(\) && this_time == res && cmp_rdata\(rdata, data\)',
 r'cmp_rdata\(rdata, data\) && rdata_fence\(\) && res == new_time',
]
for pattern in patterns:assert re.search(pattern,s),pattern
checks=s[s.index('bool spu_thread::reservation_check('):s.index('usz spu_thread::register_cache_line_waiter')]
assert checks.count('&& rdata_fence()')==6
reads=s[s.index('s64 spu_thread::get_ch_value('):s.index('bool spu_thread::set_ch_value(')]
assert reads.count('!cmp_rdata(rdata, *resrv_mem) && rdata_fence()')==4
atomic=(CORE/'util/atomic.hpp').read_text()
fence=atomic[atomic.index('FORCE_INLINE void atomic_fence_acquire()'):atomic.index('FORCE_INLINE void atomic_fence_release()')]
helper=s[s.index('static FORCE_INLINE bool rdata_fence()'):s.index('\n}',s.index('static FORCE_INLINE bool rdata_fence()'))+2]
fixture='#define FORCE_INLINE inline __attribute__((always_inline))\n'+fence+helper+'''\nextern "C" bool snapshot(const unsigned long long*data,const unsigned long long*time,unsigned long long*out){
 auto before=__atomic_load_n(time,__ATOMIC_ACQUIRE);*out=*data;rdata_fence();return before==__atomic_load_n(time,__ATOMIC_ACQUIRE);
}
extern "C" bool loss(const unsigned long long*data,unsigned long long expected){return *data!=expected&&rdata_fence();}
'''
with tempfile.TemporaryDirectory(prefix='spu-ordering-') as tmp:
    out=Path(tmp);(out/'test.cpp').write_text(fixture)
    subprocess.run([os.environ.get('CXX','clang++'),'-std=c++20','-O2','-target','arm64-apple-macos15','-S',out/'test.cpp','-o',out/'test.s'],check=True)
    asm=(out/'test.s').read_text();assert len(re.findall(r'dmb\s+ishld',asm))==2,asm
    snap=asm[asm.index('_snapshot:'):asm.index('_loss:')]
    loads=list(re.finditer(r'lda(?:p)?r\s',snap));assert len(loads)==2 and loads[0].start()<snap.index('dmb')<loads[1].start(),snap
print('SPU ordering: 16 reservation snapshot/loss sites and ARM64 DMB ISHLD placement verified.')
