#include <atomic>
#include <algorithm>
#include <chrono>
#include <vector>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
using u32=std::uint32_t;using u64=std::uint64_t;using u128=__uint128_t;using usz=std::size_t;
using spu_rdata_t=std::byte[128];
constexpr usz umax=~usz{0};
inline int config_reads=0;
struct config_bool {
    std::atomic<bool> value{false};
    explicit operator bool() const {
#ifdef COUNT_CONFIG_READS
        ++config_reads;
#endif
        return value.load();
    }
    config_bool& operator=(bool b){value=b;return *this;}
};
struct {struct {config_bool spu_accurate_reservations;} core;} g_cfg;
struct reservation
{
    u64 value=256;bool fail=false;
    operator u64()const{return value;}
    void operator+=(u64 n){value+=n;}
    bool compare_and_swap_test(u64 a,u64 b){if(fail||value!=a)return false;value=b;return true;}
    template<typename F> auto fetch_op(F f){u64 old=value,n=value;bool ok=!fail&&f(n);if(ok)value=n;return std::pair{old,ok};}
};
int locks=0,scans=0,faults=0,writer_locks=0;bool cas_fail=false;u64 change_on_lock=0;
struct range_lock_t {void release(int){}};
namespace vm
{
    alignas(16) spu_rdata_t memory{};
    reservation res;
    constexpr u64 rsrv_unique_lock=64;
    auto& reservation_acquire(u32){return res;}
    template<typename T>T& _ref(u32){return *reinterpret_cast<T*>(&memory);}
    template<typename T>T* _ptr(u32){return reinterpret_cast<T*>(&memory);}
    template<typename T>T* get_super_ptr(u32){return _ptr<T>(0);}
    void* base(u32){return memory;}
    template<int>void range_lock(range_lock_t*,u32,u32){}
    struct writer_lock {writer_lock(u32,range_lock_t*){++writer_locks;}};
}
namespace rsx {struct reservation_lock {reservation_lock(u32,u32){++locks;vm::res.value+=change_on_lock;}};}
namespace utils {void trigger_write_page_fault(void*){++faults;}}
template<typename T>struct atomic_storage {static bool compare_exchange(T& p,T& expected,T desired){if(cas_fail||p!=expected){expected=p;return false;}p=desired;return true;}};
bool cmp_rdata(const spu_rdata_t& a,const spu_rdata_t& b){return std::memcmp(a,b,128)==0;}
void mov_rdata(spu_rdata_t& a,const spu_rdata_t& b){std::memcpy(a,b,128);}
usz scan16_rdata(const spu_rdata_t& a,const spu_rdata_t& b)
{
    ++scans;usz pos=umax;
    for(usz i=0;i<8;++i)if(std::memcmp(a+16*i,b+16*i,16)){if(pos!=umax)return umax;pos=i;}
    return pos;
}
struct spu_mfc_cmd {u32 eal=0x1000,lsa=0;};
struct spu
{
    u32 raddr=0x1000,spurs_addr=0x8000;u64 rtime=256;
    alignas(16) spu_rdata_t rdata{},ls{};
    range_lock_t lock;range_lock_t* range_lock=&lock;
    template<typename T>T& _ref(u32){return *reinterpret_cast<T*>(&ls);}
#include "Putllc.inc"
};
struct result {bool ok;u32 raddr;u64 res;std::array<std::byte,128> memory;int faults,writers,locks,scans,config_reads;};

#ifndef RPCS3_PUTLLC_BENCH
int main(int argc,char** argv)
{
    assert(argc==2);std::string mode=argv[1];unsigned tested=0,noops=0,lazy=0,cfg_saved=0;
    for(bool accurate:{false,true})for(bool address_match:{false,true})for(unsigned version:{128,256,257,384})
    for(int changes=0;changes<10;++changes)for(bool spurs:{false,true})for(bool super_matches:{false,true})
    for(bool fail:{false,true})for(bool atom_fail:{false,true})for(u64 race:{0,128})
    {
        std::array<result,2> r;
        for(int variant=0;variant<2;++variant)
        {
            spu s;s.raddr=address_match?0x1000:0x2000;s.spurs_addr=spurs?0x1000:0x8000;
            for(unsigned i=0;i<128;++i)s.rdata[i]=s.ls[i]=vm::memory[i]=std::byte(i*71+13);
            if(changes>0)s.ls[((changes-1)%8)*16+3]^=std::byte{0x81};
            if(changes==9)s.ls[32]^=std::byte{1};
            if(!super_matches)vm::memory[19]^=std::byte{0x14};
            g_cfg.core.spu_accurate_reservations=accurate;vm::res={version,fail};cas_fail=atom_fail;change_on_lock=race;
            locks=scans=faults=writer_locks=config_reads=0;
            bool ok=variant?s.candidate({}):s.baseline({});
            r[variant]={ok,s.raddr,vm::res.value,{},faults,writer_locks,locks,scans,config_reads};
            std::memcpy(r[variant].memory.data(),vm::memory,128);
        }
        assert(r[0].ok==r[1].ok&&r[0].raddr==r[1].raddr&&r[0].memory==r[1].memory);
        assert(r[0].faults==r[1].faults&&r[0].writers==r[1].writers);
        // The external version update in the lock stub is not performed by an
        // early no-write return. It is not a reservation write by the candidate.
        assert(r[0].res==r[1].res+((r[0].locks-r[1].locks)*race));
        if(r[1].locks<r[0].locks){assert(address_match && ((!accurate&&!changes) || !r[1].ok));++noops;}
        if(r[1].scans<r[0].scans){assert((!accurate&&spurs) || !r[1].ok);++lazy;}
        assert(r[1].config_reads<=r[0].config_reads);cfg_saved+=r[0].config_reads-r[1].config_reads;
        ++tested;
    }
    assert(cfg_saved>0);std::printf("Saved %u atomic configuration reads across %u transactions\n",cfg_saved,tested);
    std::printf("%s: %u differential transactions passed; %u RSX locks avoided, %u unused 128-byte scans avoided\n",mode.c_str(),tested,noops,lazy);
}

#else

using Fn=bool(*)(spu&);
__attribute__((noinline)) bool old_fn(spu& s){return s.baseline({});}
__attribute__((noinline)) bool new_fn(spu& s){return s.candidate({});}
double bench(Fn fn,bool accurate,unsigned mode)
{
    spu s;g_cfg.core.spu_accurate_reservations=accurate;
    s.spurs_addr=mode==2?0x1000:0x8000;
    for(unsigned i=0;i<128;++i)s.rdata[i]=s.ls[i]=vm::memory[i]=std::byte{1};
    s.ls[3]=std::byte{2};if(mode==1)s.ls[35]=std::byte{2};
    const unsigned iterations=1000000;unsigned result=0;
    const auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<iterations;++i){
        s.raddr=0x1000;vm::res.value=mode==3?384:256;vm::memory[3]=vm::memory[35]=std::byte{1};
        asm volatile("":::"memory");result+=fn(s);
    }
    asm volatile(""::"r"(result):"memory");
    return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/iterations;
}
int main(){
    for(bool accurate:{false,true})for(unsigned mode:{0u,1u,2u,3u}){
        std::vector<double> times[2];
        for(unsigned round=0;round<10;++round)for(unsigned order=0;order<2;++order){
            unsigned v=(round+order)%2;double t=bench(v?new_fn:old_fn,accurate,mode);if(round>1)times[v].push_back(t);
        }
        for(auto& x:times)std::sort(x.begin(),x.end());
        double old=(times[0][3]+times[0][4])/2,now=(times[1][3]+times[1][4])/2;
        std::printf("accurate=%d mode=%u %.3f -> %.3f ns saving=%.2f%%\n",accurate,mode,old,now,100*(1-now/old));
    }
}

#endif
