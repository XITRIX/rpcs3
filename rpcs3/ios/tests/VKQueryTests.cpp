#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <stdexcept>
#include <string>
#include <vector>
using u32 = std::uint32_t;
using VkQueryResultFlags = u32;
constexpr u32 VK_QUERY_RESULT_PARTIAL_BIT=1, VK_QUERY_RESULT_WITH_AVAILABILITY_BIT=2, VK_QUERY_RESULT_WAIT_BIT=4;
constexpr int VK_SUCCESS=0, VK_NOT_READY=1;
#define ensure(x) assert(x)
namespace utils { void pause() {} }
namespace vk
{
enum class driver_vendor { MVK, other };
driver_vendor vendor=driver_vendor::MVK;
driver_vendor get_driver_vendor(){return vendor;}
struct command_buffer {};
struct render_device { int operator*(){return 1;} };
struct query_pool
{
    int refs=1;
    int operator*(){return 2;}
    void release(){assert(refs>0);--refs;}
    bool has_refs()const{return refs>0;}
};
struct Response {int error;u32 value,available;};
std::deque<Response> responses;
std::vector<u32> flags_seen;
bool delay_model=false;
int ticks=0, completion=0;
int vkGetQueryPoolResults(render_device&,query_pool&,u32,u32,size_t,void* result,size_t,VkQueryResultFlags flags)
{
    flags_seen.push_back(flags);
    Response r;
    if(delay_model)
    {
        if(flags & VK_QUERY_RESULT_WAIT_BIT) ticks=completion;
        ++ticks;
        r={VK_SUCCESS,0,ticks>=completion?1u:0u};
    }
    else {assert(!responses.empty());r=responses.front();responses.pop_front();}
    auto* values=static_cast<u32*>(result);values[0]=r.value;values[1]=r.available;return r.error;
}
[[noreturn]] void die_with_error(int){throw std::runtime_error("query error");}
class query_pool_manager
{
public:
    struct query_slot_info {query_pool* pool=nullptr;bool any_passed=false,active=false,ready=false;u32 data=0;};
    render_device device;
    render_device* owner=&device;
    VkQueryResultFlags result_flags=VK_QUERY_RESULT_PARTIAL_BIT;
    std::vector<query_slot_info> query_slot_status=std::vector<query_slot_info>(16);
    std::deque<u32> m_available_slots;
    int cleanups=0;
    void run_pool_cleanup(){++cleanups;}
    bool poke_query(query_slot_info&,u32,VkQueryResultFlags);
    bool check_query_status(u32);
    u32 get_query_result(u32);
    bool release_query(u32);
    void free_query(command_buffer&,u32);
#include "QueryFree.inc"
};
#include "QueryMethods.inc"
}
int main(int argc,char** argv)
{
    assert(argc==2);std::string mode=argv[1];
    bool cached=mode=="cached"||mode=="combined", waiting=mode=="wait"||mode=="combined", release=mode=="release"||mode=="combined";
    using namespace vk;
    query_pool pool;query_pool_manager q;command_buffer cmd;
    auto reset=[&]{q.query_slot_status[0]={&pool,false,true,false,0};responses.clear();flags_seen.clear();delay_model=false;};
    reset();responses={{VK_SUCCESS,7,1},{VK_SUCCESS,7,1}};
    assert(q.check_query_status(0));assert(q.check_query_status(0));
    assert(flags_seen.size()==(cached?1u:2u));
    // Completed zero, not-ready zero, partial hit, precise result and driver errors.
    reset();responses={{VK_SUCCESS,0,0},{VK_SUCCESS,0,1}};
    assert(!q.check_query_status(0));assert(q.get_query_result(0)==0);
    reset();responses={{VK_NOT_READY,9,0}};assert(q.get_query_result(0)==9);assert(flags_seen.size()==1);
    reset();q.result_flags=0;responses={{VK_NOT_READY,9,0},{VK_SUCCESS,11,1}};
    assert(q.get_query_result(0)==11);assert(bool(flags_seen.back()&VK_QUERY_RESULT_WAIT_BIT)==waiting);
    reset();responses={{-4,0,0}};bool threw=false;try{q.get_query_result(0);}catch(const std::runtime_error&){threw=true;}assert(threw);
    // Initial/unencoded query may remain unavailable even with WAIT. Re-probe.
    reset();q.result_flags=VK_QUERY_RESULT_PARTIAL_BIT;responses={{VK_SUCCESS,0,0},{VK_SUCCESS,0,0},{VK_SUCCESS,42,1}};
    assert(q.get_query_result(0)==42);assert(flags_seen.size()==3);
    for(auto driver:{driver_vendor::MVK,driver_vendor::other})
    {
        vendor=driver;reset();delay_model=true;ticks=0;completion=10000;
        assert(q.get_query_result(0)==0);
        assert(flags_seen.size()==(waiting&&driver==driver_vendor::MVK?2u:10000u));
        std::printf("%s driver=%s delayed probes=%zu\n",mode.c_str(),driver==driver_vendor::MVK?"MVK":"other",flags_seen.size());
    }
    delay_model=false;vendor=driver_vendor::MVK;
    std::array<query_pool,4> pools;std::vector<u32> ids;
    for(u32 i=0;i<4;++i){pools[i].refs=2;for(u32 j=0;j<2;++j){auto k=i*2+j;q.query_slot_status[k]={&pools[i],false,true,true,12};ids.push_back(k);}}
    q.free_queries(cmd,ids);assert(q.cleanups==(release?1:4));
    for(auto id:ids){const auto& s=q.query_slot_status[id];assert(!s.pool&&!s.active&&!s.ready&&!s.data);assert(q.m_available_slots.front()==id);q.m_available_slots.pop_front();}
    for(const auto& p:pools)assert(!p.has_refs());
    std::printf("%s release cleanup scans=%d; result, flag, error, reset, FIFO tests passed\n",mode.c_str(),q.cleanups);
    // Reuse must observe new data, not the previous cached result.
    pool.refs=1;reset();responses={{VK_SUCCESS,3,1}};assert(q.get_query_result(0)==3);
}
