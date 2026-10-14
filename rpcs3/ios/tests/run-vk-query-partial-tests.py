#!/usr/bin/env python3
"""Check production query results and bounded MoltenVK probes against a driver model."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=HERE.parents[1] / 'Emu/RSX/VK/VKQueryPool.cpp')
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--negative-control', action='store_true')
args = parser.parse_args()
source = args.source.read_text()

def method(marker):
    start = source.index(marker)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

poke = method('inline bool query_pool_manager::poke_query(')
complete = method('u32 query_pool_manager::get_query_result(')
if args.negative_control:
    complete = complete.replace('== vk::driver_vendor::MVK\n', '== vk::driver_vendor::MVK &&\n\t\t\t\t!(result_flags & VK_QUERY_RESULT_PARTIAL_BIT)\n', 1)
fixture = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
using u32=uint32_t;
using VkQueryResultFlags=u32;
constexpr u32 VK_QUERY_RESULT_PARTIAL_BIT=1, VK_QUERY_RESULT_WITH_AVAILABILITY_BIT=2, VK_QUERY_RESULT_WAIT_BIT=4;
constexpr int VK_SUCCESS=0, VK_NOT_READY=1;
namespace utils { void pause(){} }
namespace vk {
enum class driver_vendor { MVK, other };
driver_vendor vendor=driver_vendor::MVK;
driver_vendor get_driver_vendor(){return vendor;}
struct render_device {int operator*(){return 1;}};
struct query_pool {int operator*(){return 2;}};
int tick,positive_at,complete_at,probes;
int vkGetQueryPoolResults(render_device&,query_pool&,u32,u32,size_t,void* data,size_t,VkQueryResultFlags flags) {
    ++probes; ++tick;
    if ((flags & VK_QUERY_RESULT_WAIT_BIT) && tick<complete_at) tick=complete_at;
    const bool available=tick>=complete_at;
    const bool partial=flags & VK_QUERY_RESULT_PARTIAL_BIT;
    auto* out=static_cast<u32*>(data);
    out[0]=(available || partial) && tick>=positive_at ? 7 : 0;
    out[1]=available;
    return available || partial ? VK_SUCCESS : VK_NOT_READY;
}
[[noreturn]] void die_with_error(int){throw std::runtime_error("query error");}
struct query_pool_manager {
    struct query_slot_info {query_pool* pool;bool any_passed=false,active=true,ready=false;u32 data=0;};
    render_device device;query_pool pool;
    render_device* owner=&device;
    std::array<query_slot_info,1> query_slot_status{{{&pool}}};
    VkQueryResultFlags result_flags=VK_QUERY_RESULT_PARTIAL_BIT;
    bool poke_query(query_slot_info&,u32,VkQueryResultFlags);
    u32 get_query_result(u32);
};
@METHODS@
}
int main() {
    using namespace vk;
    unsigned cases=0;
    for (int positive : {1,2,3,10,50,99,100,101})
    for (bool precise : {false,true})
    for (bool other : {false,true})
    for (bool cached : {false,true}) {
        tick=0;probes=0;positive_at=positive;complete_at=100;
        vendor=other ? driver_vendor::other : driver_vendor::MVK;
        query_pool_manager q;
        q.result_flags=precise ? 0 : VK_QUERY_RESULT_PARTIAL_BIT;
        if (cached) {q.query_slot_status[0].ready=true;q.query_slot_status[0].data=7;}
        const auto value=q.get_query_result(0);
        assert(value==(cached || positive<=100 ? 7u : 0u));
        const int expected=cached ? 0 : !precise && positive<=100 && (other || positive==1) ? positive : 100;
        if (tick!=expected) {
            std::fprintf(stderr,"Query completion bypassed the native wait: positive=%d actual=%d expected=%d\n",positive,tick,expected);
            return 86;
        }
        if (!other && !cached && probes!=(!precise && positive==1 ? 1 : 2)) {
            std::fprintf(stderr,"MoltenVK completion used %d CPU probes instead of native waiting\n",probes);
            return 86;
        }
        ++cases;
    }
    std::printf("%u production-query cases: immediate partial, native wait, final zero, accurate counts, cached and non-MVK results passed. Timing is modeled.\n",cases);
}
'''.replace('@METHODS@', poke + '\n' + complete)
with tempfile.TemporaryDirectory(prefix='vk-query-partial-') as directory:
    path = Path(directory)
    cpp, exe = path/'tests.cpp', path/'tests'
    cpp.write_text(fixture)
    command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror']
    if args.sanitize:
        command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(command + [str(cpp), '-o', str(exe)], check=True)
    result = subprocess.run([str(exe)], timeout=10)
    if args.negative_control:
        assert result.returncode == 86, f'Expected unbounded polling rejection, got {result.returncode}'
        print('Unbounded polling control rejected: MoltenVK completion must retain the native wait.')
    else:
        result.check_returncode()
