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
struct {struct {bool rsx_accurate_res_access=true;} core;} g_cfg;
namespace old
{
#include "old-mutex.inc"
namespace rsx { namespace constants {constexpr u32 local_mem_base=0xc0000000;} }
#include "old-rsx.inc"
}
namespace candidate
{
#include "new-mutex.inc"
namespace rsx { namespace constants {constexpr u32 local_mem_base=0xc0000000;} }
#include "new-rsx.inc"
}

template <typename M, unsigned Attempts> bool acquire(M& m)
{
    if constexpr (Attempts==1) return m.try_lock_shared();
    else return m.try_lock_shared(Attempts);
}
template <typename M,unsigned Attempts> void stress(bool writer)
{
    M m;
    unsigned data=0;
    std::atomic<unsigned> readers=0;
    std::atomic<bool> writing=false;
    std::vector<std::thread> threads;
    for (unsigned t=0;t<6;++t) threads.emplace_back([&]{
        for (unsigned i=0;i<30000;++i) {
            if (!acquire<M,Attempts>(m)) m.lock_shared();
            ++readers; assert(!writing); (void)data; --readers; m.unlock_shared();
        }
    });
    if (writer) threads.emplace_back([&]{for (unsigned i=0;i<4000;++i) {
        m.lock(); writing=true; assert(readers==0); ++data; writing=false; m.unlock();
    }});
    for (auto& t:threads) t.join();
    assert(m.is_free()&&data==(writer?4000u:0u));
}

template <typename M,unsigned Attempts> double bench(unsigned threads_count,bool writer)
{
    M m;
    std::barrier start(static_cast<std::ptrdiff_t>(threads_count+1+writer));
    std::vector<std::thread> threads;
    std::atomic<unsigned> failures=0;
    for (unsigned t=0;t<threads_count;++t) threads.emplace_back([&]{start.arrive_and_wait();unsigned f=0;
        for (unsigned i=0;i<120000;++i) {
            if (!acquire<M,Attempts>(m)) {++f;m.lock_shared();}
            asm volatile("":::"memory");m.unlock_shared();
        } failures+=f;
    });
    if (writer) threads.emplace_back([&]{start.arrive_and_wait();for(unsigned i=0;i<5000;++i){m.lock();m.unlock();}});
    auto then=std::chrono::steady_clock::now();start.arrive_and_wait();
    for (auto& t:threads)t.join();
    assert(m.is_free());
    return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-then).count()/(threads_count*120000);
}
int main(int argc,char**)
{
    // Bounded reader retries cannot cross a queued writer, saturation or signal.
    for (u32 value : {0u,1u,16382u,16383u,16384u,1073741824u,2147483648u}) {
        candidate::shared_mutex m;
        injected_cas_values={value};injections=0;
        bool ok=m.try_lock_shared(8);
        assert(ok==(value<16383));
        if (ok) m.unlock_shared();
    }
    injected_cas_values={1,2,3,16384};injections=0;
    {candidate::shared_mutex m;assert(!m.try_lock_shared(8));assert(injections==4);}
    injected_cas_values={1,2,3,4,5,6,7,8,9};injections=0;
    {candidate::shared_mutex m;assert(!m.try_lock_shared(8));assert(injections==8);}
    injected_cas_values.clear();injections=0;
    stress<old::shared_mutex,1>(false);stress<candidate::shared_mutex,8>(false);
    stress<old::shared_mutex,1>(true);stress<candidate::shared_mutex,8>(true);
    cpu_thread cpu;current=&cpu;
    cpu_reads=renderer_reads=0;
    {old::rsx::reservation_lock<> lock(0x10000,128);}
    assert(cpu_reads==1&&renderer_reads==2);
    cpu_reads=renderer_reads=0;
    {candidate::rsx::reservation_lock<> lock(0x10000,128);}
    assert(cpu_reads==0&&renderer_reads==1);
    // Exercise the generic loop too: the single-range shortcut must not hide
    // eager TLS lookup in shared multi-lock or exclusive acquisitions.
    cpu_reads=renderer_reads=0;
    {candidate::rsx::reservation_lock<false,1> lock(0x10000,16384);}
    assert(cpu_reads==0&&renderer_reads==1);
    cpu_reads=renderer_reads=0;
    {candidate::rsx::reservation_lock<true,1> lock(0x10000,16384);}
    assert(cpu_reads==0&&renderer_reads==1);
    // Batched lock moves must acquire/release exactly the same lock sections.
    for (u32 addr : {0u,1u,8191u,8192u,65535u,0x100000u}) for(u32 len:{1u,2u,128u,8192u,16384u}) {
        {candidate::rsx::reservation_lock<false,1> lock(addr,len,true);
         lock.update_if_enabled(0x80000,128); lock.unlock(); lock.update_if_enabled(0x90000,128);}
        for (u32 i=0;i<256;++i) assert(candidate::renderer.iomap_table.rs[i].is_free());
    }
    for (bool pre_wait : {false,true}) for(auto kind:{thread_class::spu,thread_class::rsx}) {
        cpu.state.wait=pre_wait;cpu.kind=kind;cpu.checked=cpu.waited=0;cpu_reads=0;
        auto& m=candidate::renderer.iomap_table.rs[8];m.lock();
        std::thread release([&]{std::this_thread::sleep_for(std::chrono::milliseconds(3));m.unlock();});
        {candidate::rsx::reservation_lock<> lock(65536,128);}
        release.join();assert(cpu_reads==1&&cpu.checked==unsigned(!pre_wait)&&m.is_free());
        if(kind==thread_class::rsx) assert(cpu.waited>0);
    }
    current=nullptr;
    std::puts("PASS: mutex reader/writer exclusion, retry bounds, writer/saturation/signal races, RSX wait ownership, lock moves, lazy TLS and renderer lookup counts");
    if(argc>1) for (unsigned n:{1u,4u,6u}) for(bool writer:{false,true}) {
        std::vector<double> times[2];
        for(unsigned round=0;round<8;++round)for(unsigned order=0;order<2;++order) {
            unsigned v=(round+order)%2;
            auto time=v?bench<candidate::shared_mutex,8>(n,writer):bench<old::shared_mutex,1>(n,writer);
            if(round>1)times[v].push_back(time);
        }
        for(auto& t:times)std::sort(t.begin(),t.end());
        double old=(times[0][2]+times[0][3])/2,now=(times[1][2]+times[1][3])/2;
        std::printf("readers=%u writer=%d baseline=%.3f candidate=%.3f ns/op saving=%.2f%%\n",n,writer,old,now,100*(1-now/old));
    }
}
