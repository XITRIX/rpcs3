#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <utility>
#include <vector>
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using uint = unsigned;
#define RPCS3_IOS 1
#define ensure(x) assert(x)
#include "sync-wait.inc"

// The fixture executes the production CAS and slow-path bodies. Only the
// atomic waiting backend is replaced by the C++20 host wait/notify primitive.
inline thread_local std::vector<u32> injected_cas_values;
inline thread_local unsigned injections = 0;
template <typename T> struct atomic_t
{
    std::atomic<T> value{};
    T load() const { return value.load(); }
    operator T() const { return load(); }
    bool compare_exchange(T& expected, T desired)
    {
        if (injections < injected_cas_values.size())
            value.store(injected_cas_values[injections++]);
        return value.compare_exchange_strong(expected, desired);
    }
    bool compare_and_swap_test(T expected, T desired) { return compare_exchange(expected, desired); }
    T compare_and_swap(T expected, T desired) { compare_exchange(expected,desired); return expected; }
    T fetch_add(T n) { return value.fetch_add(n); }
    T fetch_sub(T n) { return value.fetch_sub(n); }
    void operator+=(T n) { value.fetch_add(n); }
    void operator-=(T n) { value.fetch_sub(n); }
    std::atomic<T>& raw() { return value; }
    void wait(T n) { value.wait(n); }
    void notify_one() { value.notify_one(); }
    template <typename F> auto fetch_op(F f)
    {
        T old = value.load();
        for (;;) { T next=old; if (!f(next)) return std::pair{old,false};
            if (value.compare_exchange_strong(old,next)) return std::pair{old,true}; }
    }
};
template <typename T> struct atomic_storage
{
    static bool compare_exchange_hle_acq(std::atomic<T>& v,T& e,T n) {return v.compare_exchange_strong(e,n);}
    static T fetch_add_hle_rel(std::atomic<T>& v,T n) {return v.fetch_add(n);}
};
enum class thread_class { spu,rsx };
enum class cpu_flag { wait };
struct cpu_thread
{
    struct flags { bool wait=false; bool test_and_set(cpu_flag) {return std::exchange(wait,true);} } state;
    thread_class kind=thread_class::spu;
    unsigned checked=0, waited=0;
    thread_class get_class() const {return kind;}
    void check_state() {++checked; state.wait=false;}
    void cpu_wait(std::nullptr_t) {++waited;std::this_thread::yield();}
};
inline thread_local cpu_thread* current=nullptr;
inline thread_local unsigned cpu_reads=0,renderer_reads=0;
cpu_thread* get_current_cpu_thread() {++cpu_reads; return current;}
namespace utils
{
struct address_range32
{
    u32 start,end;
    static address_range32 start_length(u32 a,u32 n) {return {a,a+n-1};}
    bool overlaps(address_range32 r) const {return start<=r.end&&r.start<=end;}
    address_range32 get_min_max(address_range32 r) const {return {std::min(start,r.start),std::max(end,r.end)};}
    u32 length() const {return end-start+1;}
};
}
namespace old
{
#include "old-mutex.inc"
#include "old-rsx.inc"
}
namespace candidate
{
#include "new-mutex.inc"
#include "new-rsx.inc"
}


template<bool Full,unsigned Stride,typename Table> void check_table(Table& t,u32 addr,u32 len)
{
    const bool ok=t.template lock<Full,Stride>(addr,len);
    assert(ok==(len>1));
    const u32 first=addr/8192,last=(addr+len-1)/8192;
    if(first<=last) for(u32 k=first;k<=last&&k<t.rs.size();++k)
        assert(t.rs[k].is_free() != (len>1 && (k-first)%Stride==0));
    if(ok)t.template unlock<Full,Stride>(addr,len);
    if(first<=last)for(u32 k=first;k<=last&&k<t.rs.size();++k)assert(t.rs[k].is_free());
}
template<bool Full,unsigned Stride> void check(u32 addr,u32 len)
{
    check_table<Full,Stride>(old::renderer.iomap_table,addr,len);
    check_table<Full,Stride>(candidate::renderer.iomap_table,addr,len);
}
template<typename Table> void blocked(Table& t)
{
    for(bool waiting:{false,true}) for(auto kind:{thread_class::spu,thread_class::rsx}) {
        cpu_thread cpu;cpu.kind=kind;cpu.state.wait=waiting;current=&cpu;cpu_reads=0;
        t.rs[8].lock();
        std::thread release([&]{std::this_thread::sleep_for(std::chrono::milliseconds(2));t.rs[8].unlock();});
        assert((t.template lock<false,128>(65536,128)));release.join();
        assert(cpu_reads==1&&cpu.checked==unsigned(!waiting));
        if(kind==thread_class::rsx)assert(cpu.waited>0);
        t.template unlock<false,128>(65536,128);assert(t.rs[8].is_free());
        current=nullptr;
    }
}
template<typename Table> double bench(Table& table,unsigned workers,bool writer,bool shared_address)
{
    constexpr unsigned iterations=600000;
    std::barrier start(static_cast<std::ptrdiff_t>(workers+1+writer));
    std::vector<std::thread> threads;
    for(unsigned t=0;t<workers;++t)threads.emplace_back([&,t]{
        start.arrive_and_wait();
        for(unsigned i=0;i<iterations;++i){
            const u32 addr=(shared_address?8u:8u+t)*8192;
            const bool held=table.template lock<false,128>(addr,128);
            asm volatile("":::"memory");
            if(!held)std::abort();
            table.template unlock<false,128>(addr,128);
        }
    });
    if(writer)threads.emplace_back([&]{start.arrive_and_wait();for(unsigned i=0;i<2000;++i){table.rs[8].lock();table.rs[8].unlock();}});
    const auto then=std::chrono::steady_clock::now();start.arrive_and_wait();
    for(auto& t:threads)t.join();
    return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-then).count()/(workers*iterations);
}
int main(int argc,char**)
{
    for(u32 a:{0u,1u,8191u,8192u,65535u,0x100000u,0xfffffff0u})
      for(u32 n:{0u,1u,2u,128u,8192u,65536u}){
        check<false,1>(a,n);check<false,128>(a,n);
        check<true,1>(a,n);check<true,128>(a,n);
      }
    u32 seed=73;
    for(unsigned i=0;i<20000;++i){seed=seed*1664525u+1013904223u;const u32 a=seed;seed=seed*1664525u+1013904223u;const u32 n=seed&65535u;check<false,128>(a,n);check<true,1>(a,n);}
    blocked(old::renderer.iomap_table);blocked(candidate::renderer.iomap_table);
    for(u32 i=0;i<old::renderer.iomap_table.rs.size();++i)
        assert(old::renderer.iomap_table.rs[i].is_free()&&candidate::renderer.iomap_table.rs[i].is_free());
    std::puts("PASS: single/multiple/wrapped ranges, shared/exclusive locks, writer contention and wait ownership");
    if(argc==1)return 0;
    for(unsigned workers:{1u,4u,6u})for(bool shared:{false,true})for(bool writer:{false,true}){
        std::vector<double> times[2];
        for(unsigned round=0;round<8;++round)for(unsigned order=0;order<2;++order){
            unsigned v=(round+order)%2;
            double ns=v?bench(candidate::renderer.iomap_table,workers,writer,shared):bench(old::renderer.iomap_table,workers,writer,shared);
            if(round>1)times[v].push_back(ns);
        }
        for(auto& x:times)std::sort(x.begin(),x.end());
        double old=(times[0][2]+times[0][3])/2,now=(times[1][2]+times[1][3])/2;
        std::printf("readers=%u same=%d writer=%d %.3f -> %.3f ns saving=%.2f%%\n",workers,shared,writer,old,now,100*(1-now/old));
    }
}
