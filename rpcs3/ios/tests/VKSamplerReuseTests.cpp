#include <cassert>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>
using VkSamplerAddressMode=int;using VkBool32=int;using VkFilter=int;using VkSamplerMipmapMode=int;using VkCompareOp=int;
constexpr int VK_FALSE=0,VK_COMPARE_OP_NEVER=0;
#define ensure(x) assert(x)
namespace vk
{
struct render_device{};struct border_color_t{};struct sampler{};
struct key_t{std::uint64_t base_key=0,border_color_key=0;bool operator==(const key_t&)const=default;};
struct cached_sampler_object_t:sampler
{
    key_t key;unsigned refs=0,mutations=0;
    template<typename... T>cached_sampler_object_t(T&&...){}
    bool has_refs()const{return refs>0;}
    void release(){assert(refs);--refs;++mutations;}
    void add_ref(){++refs;++mutations;}
};
struct pool
{
    key_t next;unsigned finds=0;
    std::vector<std::unique_ptr<cached_sampler_object_t>> objects;
    template<typename...T>key_t compute_storage_key(T&&...){return next;}
    cached_sampler_object_t* find(const key_t& key){++finds;for(auto& p:objects)if(p->key==key)return p.get();return nullptr;}
    cached_sampler_object_t* emplace(const key_t& key,std::unique_ptr<cached_sampler_object_t>& p){p->key=key;auto* ret=p.get();objects.push_back(std::move(p));return ret;}
};
struct manager
{
    pool m_sampler_pool;
#include "old-sampler.inc"
#include "new-sampler.inc"
    cached_sampler_object_t* request(bool candidate,sampler* previous,key_t key)
    {
        m_sampler_pool.next=key;render_device dev;border_color_t color;
        return static_cast<cached_sampler_object_t*>(candidate?new_sampler(dev,previous,0,0,0,0,0,0,0,0,0,0,0,color):old_sampler(dev,previous,0,0,0,0,0,0,0,0,0,0,0,color));
    }
};
}
int main()
{
    using namespace vk;
    unsigned finds[2]{},mutations[2]{};
    for(bool candidate:{false,true})
    {
        manager m;auto* p=m.request(candidate,nullptr,{1,2});assert(p->refs==1);
        m.m_sampler_pool.finds=0;p->mutations=0;
        for(int i=0;i<10000;++i){assert(m.request(candidate,p,{1,2})==p);assert(p->refs==1);}
        finds[candidate]=m.m_sampler_pool.finds;mutations[candidate]=p->mutations;
        // Every bit in both halves of the key must defeat reuse independently.
        for(unsigned bit=0;bit<128;++bit)
        {
            vk::key_t changed{1,2};if(bit<64)changed.base_key^=1ull<<bit;else changed.border_color_key^=1ull<<(bit-64);
            auto* next=m.request(candidate,p,changed);assert(next!=p&&next->key==changed&&next->refs==1&&p->refs==0);
            auto* restored=m.request(candidate,next,{1,2});assert(restored==p&&p->refs==1&&next->refs==0);
        }
        // Shared ownership and an absent previous binding retain exact counts.
        auto* alias=m.request(candidate,nullptr,{1,2});assert(alias==p&&p->refs==2);
        assert(m.request(candidate,p,{1,2})==p&&p->refs==2);
        auto* custom=m.request(candidate,p,{1,3});assert(custom!=p&&custom->refs==1&&p->refs==1);
        p->release();custom->release();for(auto& object:m.m_sampler_pool.objects)assert(object->refs==0);
    }
    assert(finds[0]==10000&&finds[1]==0&&mutations[0]==20000&&mutations[1]==0);
    printf("sampler reuse: 128 single-bit key differences and ownership cases passed; repeated lookups %u -> %u, reference mutations %u -> %u\n",finds[0],finds[1],mutations[0],mutations[1]);
}
