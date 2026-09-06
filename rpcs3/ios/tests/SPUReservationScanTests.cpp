#include "Emu/CPU/Backends/AArch64/SPUReservationScan.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>
using Fn = std::size_t(*)(const void*, const void*);
__attribute__((noinline)) std::size_t candidate(const void* a, const void* b) { return aarch64::spu_scan16_rdata(a,b); }
// The existing v128 != reduction lowers to XOR, signed saturating narrow,
// one scalar transfer, and a scalar nonzero test for each 16-byte block.
__attribute__((noinline)) std::size_t baseline(const void* av, const void* bv)
{
    auto* a=static_cast<const uint8_t*>(av);auto* b=static_cast<const uint8_t*>(bv);
    uint32_t mask=0;
    for (unsigned i=0;i<8;++i) {
        auto d=veorq_u8(vld1q_u8(a+16*i),vld1q_u8(b+16*i));
        auto n=vqmovn_s32(vreinterpretq_s32_u8(d));
        mask|=uint32_t(vget_lane_u64(vreinterpret_u64_s16(n),0)!=0)<<i;
    }
    return mask && !(mask&(mask-1)) ? std::countr_zero(mask) : ~std::size_t{0};
}
std::size_t oracle(const uint8_t* a,const uint8_t* b)
{
    std::size_t found=~std::size_t{0};
    for(unsigned i=0;i<8;++i) if(std::memcmp(a+16*i,b+16*i,16)) {
        if(found!=~std::size_t{0}) return ~std::size_t{0}; found=i;
    }
    return found;
}
uint64_t cases=0;
void check(const uint8_t* a,const uint8_t* b)
{
    if(candidate(a,b)!=oracle(a,b)||baseline(a,b)!=oracle(a,b)) std::abort();
    ++cases;
}
__attribute__((noinline)) double bench(Fn fn,const uint8_t* a,const uint8_t* b,unsigned entries)
{
    constexpr unsigned n=2000000;
    std::size_t sum=0;
    auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<n;++i) {
        auto pos=(i%entries)*128;
        asm volatile("" ::: "memory");
        sum+=fn(a+pos,b+pos);
    }
    auto end=std::chrono::steady_clock::now();
    asm volatile("" :: "r"(sum) : "memory");
    return std::chrono::duration<double,std::nano>(end-start).count()/n;
}
int main(int argc, char**)
{
    alignas(128) std::array<uint8_t,160> a{},b{};
    for(unsigned alignment=0;alignment<16;++alignment) {
        auto* x=a.data()+alignment;auto* y=b.data()+alignment;
        std::memset(x,0,128);std::memset(y,0,128);check(x,y);
        for(unsigned bit=0;bit<1024;++bit) {y[bit/8]^=1u<<(bit%8);check(x,y);y[bit/8]^=1u<<(bit%8);}
        for(unsigned mask=0;mask<256;++mask) {
            std::memset(y,0,128);
            for(unsigned block=0;block<8;++block) if(mask&(1u<<block)) y[block*16+(block*3)%16]=128;
            check(x,y);
        }
    }
    std::mt19937_64 rng(170);
    for(unsigned trial=0;trial<200000;++trial) {
        for(auto& v:a) v=uint8_t(rng()); b=a;
        unsigned mutations=rng()%140;
        for(unsigned j=0;j<mutations;++j) b[rng()%128]^=uint8_t(rng());
        check(a.data(),b.data());
    }
    // A full reservation directly beside an inaccessible page catches overreads.
    const auto page=static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    auto* ma=static_cast<uint8_t*>(mmap(nullptr,3*page,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0));
    auto* mb=static_cast<uint8_t*>(mmap(nullptr,3*page,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0));
    if(ma==MAP_FAILED || mb==MAP_FAILED) std::abort();
    if(mprotect(ma+page,page,PROT_READ|PROT_WRITE) || mprotect(mb+page,page,PROT_READ|PROT_WRITE)) std::abort();
    for(auto offset:{page,2*page-128}) {
        auto* x=ma+offset;auto* y=mb+offset;
        std::memset(x,0,128);std::memset(y,0,128);check(x,y);
        for(unsigned bit=0;bit<1024;++bit) {y[bit/8]^=1u<<(bit%8);check(x,y);y[bit/8]^=1u<<(bit%8);}
    }
    munmap(ma,3*page);munmap(mb,3*page);
    std::printf("%llu cases passed\n",static_cast<unsigned long long>(cases));
    if (argc == 1) return 0;
    for(unsigned entries:{1u,256u,16384u}) for(unsigned changed:{0u,1u,8u}) {
        std::vector<uint8_t> x(entries*128),y(entries*128);
        for(unsigned i=0;i<entries;++i) for(unsigned j=0;j<changed;++j) y[128*i+16*((j+i)%8)]=128;
        std::vector<double> old,newer;
        for(unsigned round=0;round<9;++round) {
            if(round%2) {newer.push_back(bench(candidate,x.data(),y.data(),entries));old.push_back(bench(baseline,x.data(),y.data(),entries));}
            else {old.push_back(bench(baseline,x.data(),y.data(),entries));newer.push_back(bench(candidate,x.data(),y.data(),entries));}
        }
        std::sort(old.begin(),old.end());std::sort(newer.begin(),newer.end());
        std::printf("entries=%u changed=%u old=%.3f new=%.3f ns speedup=%.3fx\n",entries,changed,old[4],newer[4],old[4]/newer[4]);
    }
}
