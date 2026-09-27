#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <utility>
#include <vector>
using u16=std::uint16_t;using u32=std::uint32_t;using u64=std::uint64_t;
struct {struct {u32 mfc_transfers_shuffling=0;} core;} g_cfg;
bool timed=false;u64 timestamp=0;unsigned timestamp_reads=0,event_reads=0;
namespace utils {u64 get_tsc(){++timestamp_reads;if(!timed)return timestamp;u64 r;asm volatile("mrs %0, cntvct_el0":"=r"(r));return r;}}
struct thread
{
    bool interrupts_enabled=false;u32 pc=0,srr0=0,instruction=0,cancellations=0;
    void cancel_dec_interrupt(){++cancellations;}
    struct {std::atomic<u32> count{0};struct value{u32 count;};value load(){++event_reads;return {count.load()};}} ch_events;
    template <typename T>T _ref(u32){return instruction;}
#include "old-mfc.inc"
#include "new-mfc.inc"
};
volatile u64 sink;
int main(int argc,char**)
{
    thread t;unsigned checks=0;
    std::mt19937_64 rng(258);
    for(u32 limit=0;limit<=16;++limit)for(u32 size=0;size<=16;++size)for(unsigned n=0;n<256;++n){
        timestamp=rng();g_cfg.core.mfc_transfers_shuffling=limit;timestamp_reads=0;
        auto a=t.mask_old(size),b=t.mask_new(size);assert(a==b);
        assert(timestamp_reads==2);++checks;
    }
    for(bool enabled:{false,true})for(u32 count:{0u,1u,2u,0xffffffffu})for(unsigned n=0;n<10000;++n){
        u32 instruction=rng(),pc=rng(),srr0=rng(),next=rng();
        t.instruction=instruction;t.pc=pc;t.srr0=srr0;t.interrupts_enabled=enabled;t.ch_events.count=count;
        bool a=t.interrupt_old(next);auto pa=t.pc,sa=t.srr0;bool ea=t.interrupts_enabled;
        t.pc=pc;t.srr0=srr0;t.interrupts_enabled=enabled;event_reads=0;
        t.cancellations=0;bool b=t.interrupt_new(next);assert(t.cancellations==unsigned(b));assert(a==b&&pa==t.pc&&sa==t.srr0&&ea==t.interrupts_enabled);
        assert(event_reads==unsigned(enabled));++checks;
    }
    std::printf("PASS MFC: %u mask/interrupt/guest-PC differential cases; disabled interrupts omit event reads\n",checks);
    if(argc==1)return 0;
    timed=true;
    for(unsigned mode=0;mode<2;++mode){std::vector<double> values[2];
        for(unsigned round=0;round<10;++round)for(unsigned order=0;order<2;++order){unsigned v=(round+order)%2;
            g_cfg.core.mfc_transfers_shuffling=0;t.interrupts_enabled=false;t.ch_events.count=1;
            auto start=std::chrono::steady_clock::now();u64 sum=0;
            for(unsigned i=0;i<3000000;++i){asm volatile("":::"memory");
                if(mode==0)sum+=v?t.mask_new(i%17):t.mask_old(i%17);
                else sum+=v?t.interrupt_new(i):t.interrupt_old(i);
            }
            sink=sum;double ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/3000000;
            if(round>=2)values[v].push_back(ns);
        }
        for(auto& a:values)std::sort(a.begin(),a.end());double a=(values[0][3]+values[0][4])/2,b=(values[1][3]+values[1][4])/2;
        std::printf("%s baseline=%.3f candidate=%.3f ns/call saving=%.2f%%\n",mode?"disabled_interrupt":"unshuffled_mask",a,b,100*(1-b/a));
    }
}
