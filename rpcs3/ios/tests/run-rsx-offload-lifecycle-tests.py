#!/usr/bin/env python3
"""Run the production offloader synchronization/teardown methods with deterministic progress."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
HERE=Path(__file__).resolve().parent
parser=argparse.ArgumentParser()
parser.add_argument('--source',type=Path,default=HERE.parents[1]/'Emu/RSX/RSXOffload.cpp')
parser.add_argument('--sanitize',action='store_true')
args=parser.parse_args();src=args.source.read_text()
code=src[src.index('\tbool dma_manager::is_current_thread()'):src.index('\tvoid dma_manager::set_mem_fault_flag()')]
fixture=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <memory>
using u32=uint32_t;using u64=uint64_t;
enum class thread_state{created,aborting};struct atomic_wait_timeout{u64 value;};
struct counter{u64 value=0;static inline unsigned waits=0;u64 load()const{return value;}void wait(u64,atomic_wait_timeout t){assert(t.value>0);++waits;value+=2000;}};
struct worker{void* current_thread_=nullptr;counter m_enqueued_count,m_processed_count;thread_state state=thread_state::created;
 operator thread_state()const{return state;}worker& operator=(thread_state x){state=x;return *this;}};
worker* active=nullptr;unsigned pauses=0;
namespace utils{void pause(){++pauses;if(pauses==1000 && active)active->m_processed_count.value=active->m_enqueued_count.value;}}
namespace thread_ctrl{void* current=nullptr;void* get_current(){return current;}}
struct renderer{bool current=true;unsigned callbacks=0;bool is_current_thread(){return current;}void on_semaphore_acquire_wait(){++callbacks;}} gpu;
renderer* get_current_renderer(){return &gpu;}
struct dma_manager{std::unique_ptr<worker>m_thread;bool m_mem_fault_flag=false,m_wait_parking=true;
 bool is_current_thread()const;bool sync()const;void join();};
#include "production.inc"
int main(){
 dma_manager d;thread_ctrl::current=&gpu;assert(!d.is_current_thread());assert(d.sync());d.join();d.join();
 assert(gpu.callbacks==0&&counter::waits==0&&pauses==0);
 d.m_thread=std::make_unique<worker>();active=d.m_thread.get();assert(d.sync());
 active->current_thread_=&gpu;assert(d.is_current_thread());thread_ctrl::current=nullptr;assert(!d.is_current_thread());
 for(bool parking:{false,true})for(bool render:{false,true}){
  gpu.current=render;gpu.callbacks=0;counter::waits=0;pauses=0;d.m_wait_parking=parking;
  active->m_processed_count.value=0;active->m_enqueued_count.value=1000;
  assert(d.sync());assert(active->m_processed_count.load()>=1000);
  assert((counter::waits>0)==(parking&&render));assert((gpu.callbacks>0)==render);
 }
 gpu.current=true;d.m_mem_fault_flag=true;active->m_processed_count.value=0;assert(!d.sync());
 d.m_mem_fault_flag=false;active->m_processed_count.value=1000;d.join();assert(active->state==thread_state::aborting);d.join();
 d.m_thread.reset();active=nullptr;assert(d.sync());d.join();
 puts("RSX offloader: absent worker, progress, parking, fault recovery and repeated teardown passed.");
}
'''
with tempfile.TemporaryDirectory(prefix='rsx-lifecycle-') as temp:
    out=Path(temp);(out/'production.inc').write_text(code);(out/'tests.cpp').write_text(fixture)
    command=[os.environ.get('CXX','clang++'),'-std=c++20','-O2','-Wall','-Wextra','-Werror','-I',str(out),str(out/'tests.cpp'),'-o',str(out/'tests')]
    if args.sanitize:command[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run(command,check=True);subprocess.run([out/'tests'],check=True)
