#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
using u32=std::uint32_t;
#define ensure(x) assert(x)
#include "Emu/RSX/VK/vkutils/query_slot_queue.hpp"
std::vector<unsigned> old_iteration(u32 mask){std::vector<unsigned> result;
#include "old-iteration.inc"
return result;}
std::vector<unsigned> new_iteration(u32 mask){std::vector<unsigned> result;
#include "new-iteration.inc"
return result;}
struct hash {static inline unsigned calls=0;size_t operator()(int k)const{++calls;return k%7;}};
struct result{int* p;int* vp;int* fp;};
struct cache
{
    std::unordered_map<int,std::unique_ptr<int>,hash> m_storage;
    std::unique_ptr<int> __null_pipeline_handle;
    bool m_cache_miss_flag=true;int vertex_program=1,fragment_program=2;
    result old_publish(int key){m_cache_miss_flag=true;
#include "old-pipeline.inc"
return {nullptr,&vertex_program,&fragment_program};}
    result new_publish(int key){m_cache_miss_flag=true;
#include "new-pipeline.inc"
return {nullptr,&vertex_program,&fragment_program};}
};
volatile u32 sink;
template<typename Q>double bench(Q& q)
{
    auto st=std::chrono::steady_clock::now();u32 sum=0;
    for(unsigned i=0;i<2000000;++i){auto x=q.front();q.pop_front();q.push_back(x);sum+=x;asm volatile("":::"memory");}
    sink=sum;return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-st).count()/2000000;
}
int main(int argc,char**argv)
{
    assert(argc==2);std::string mode=argv[1];
    if(mode=="iteration")
    {
        unsigned old_steps=0,new_steps=0;
        for(u32 m=0;m<65536;++m){auto a=old_iteration(m),b=new_iteration(m);assert(a==b);for(u32 x=m;x;x>>=1)++old_steps;new_steps+=b.size();}
        printf("binding iteration: all 65536 masks preserve ascending visits; loop steps %u -> %u\n",old_steps,new_steps);
    }
    else if(mode=="pipeline")
    {
        cache a,b;unsigned old_hash=0,new_hash=0;std::mt19937 gen(255);
        for(unsigned i=0;i<20000;++i)
        {
            int k=gen()%300;
            if(i%7==0){a.m_storage[k]=std::make_unique<int>(k+1);b.m_storage[k]=std::make_unique<int>(k+1);}
            hash::calls=0;auto ra=a.old_publish(k);old_hash+=hash::calls;
            hash::calls=0;auto rb=b.new_publish(k);new_hash+=hash::calls;
            assert(bool(ra.p)==bool(rb.p));if(ra.p)assert(*ra.p==*rb.p);
            assert(a.m_cache_miss_flag==b.m_cache_miss_flag&&a.m_storage.size()==b.m_storage.size());
            assert(!a.__null_pipeline_handle&&!b.__null_pipeline_handle);
        }
        assert(new_hash<old_hash);printf("pipeline: 20000 miss, pending, completed and collision cases pass; hash calls %u -> %u\n",old_hash,new_hash);
    }
    else if(mode=="queue")
    {
        for(u32 capacity:{1,2,3,17,1024,4096})
        {
            vk::query_slot_queue q;q.set_capacity(capacity);std::deque<u32> ref;std::mt19937 gen(255+capacity);
            for(u32 i=0;i<capacity;++i){q.push_back(i);ref.push_back(i);}
            for(unsigned i=0;i<200000;++i){if(!ref.empty()&&(ref.size()==capacity||(gen()&1))){assert(!q.empty()&&q.front()==ref.front());q.pop_front();ref.pop_front();}else{auto v=gen();q.push_back(v);ref.push_back(v);}assert(q.empty()==ref.empty());}
            while(!ref.empty()){assert(q.front()==ref.front());q.pop_front();ref.pop_front();}assert(q.empty());
        }
        vk::query_slot_queue q;q.set_capacity(4096);std::deque<u32> ref;for(u32 i=0;i<4096;++i){q.push_back(i);ref.push_back(i);}
        std::vector<double> times[2];for(int round=0;round<10;++round)for(int j=0;j<2;++j){int v=j^(round&1);auto t=v?bench(q):bench(ref);if(round>=2)times[v].push_back(t);}
        double med[2];for(int v=0;v<2;++v){std::sort(times[v].begin(),times[v].end());med[v]=(times[v][3]+times[v][4])/2;}
        printf("query FIFO: 1200000 randomized operations pass; deque %.3f ns, ring %.3f ns, saving %.2f%%\n",med[0],med[1],100*(1-med[1]/med[0]));
    }
    else return 2;
}
