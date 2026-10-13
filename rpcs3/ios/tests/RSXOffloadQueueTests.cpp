#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
using u8 = uint8_t; using u32 = uint32_t; using u64 = uint64_t;
using namespace std::string_view_literals;
#define ensure(x) assert(x)
enum class thread_state { created, aborting };
enum class thread_scheduler_mode { os };
enum class thread_class { rsx };
struct atomic_wait_timeout { u64 value; };
template<class T> struct atomic_t
{
    std::atomic<T> value;
    atomic_t(T v = {}) : value(v) {}
    T load() const { return value.load(); }
    operator T() const { return load(); }
    void release(T v) { value.store(v); }
    void operator=(T v) { release(v); }
    T operator++(int) { return value.fetch_add(1); }
    bool compare_and_swap_test(T old, T next) { return value.compare_exchange_strong(old, next); }
    void notify_all() { value.notify_all(); }
    void wait(T old, atomic_wait_timeout)
    {
        if (load() == old) std::this_thread::sleep_for(std::chrono::microseconds(20));
    }
};
struct thread_base { atomic_t<thread_state> status{thread_state::created}; };
namespace thread_ctrl
{
    thread_local thread_base* current = nullptr;
    thread_base* get_current() { return current; }
    thread_state state() { return current->status.load(); }
    u64 get_affinity_mask(thread_class) { return 0; }
    void set_thread_affinity_mask(u64) {}
}
namespace utils
{
    struct address_range32 {};
    thread_local unsigned pauses = 0;
    void pause()
    {
        if (++pauses > 2'000'000) throw std::runtime_error("queue did not progress");
        std::this_thread::yield();
    }
}
template<class T> struct lf_queue
{
    std::mutex mutex;
    std::deque<T> queue;
    atomic_t<u32> pending;
    template<class... A> void push(A&&... args)
    {
        std::lock_guard lock(mutex);
        queue.emplace_back(std::forward<A>(args)...);
        pending = 1;
    }
    std::deque<T> pop_all()
    {
        std::lock_guard lock(mutex);
        std::deque<T> batch;
        batch.swap(queue);
        pending = 0;
        return batch;
    }
    atomic_t<u32>& get_wait_atomic() { return pending; }
};
namespace rsx
{
    enum class primitive_type { quads };
    std::atomic<unsigned> reservations{0};
    template<bool, unsigned> struct reservation_lock
    {
        reservation_lock(u32 addr, u32 size, bool lock)
        {
            assert(size != 0);
            if (lock) { assert(addr == 0x10000); ++reservations; }
        }
    };
    struct renderer : thread_base
    {
        std::thread::id owner = std::this_thread::get_id();
        std::function<void(u32, void*)> callback;
        bool is_current_thread() { return owner == std::this_thread::get_id(); }
        void on_semaphore_acquire_wait() { std::this_thread::yield(); }
        void renderctl(u32 cmd, void* data) { callback(cmd, data); }
    } gpu;
    renderer* get_current_renderer() { return &gpu; }
    void write_index_array_for_non_indexed_non_native_primitive_to_buffer(char* dst, primitive_type, u32 count)
    {
        for (u32 i = 0; i < count; ++i) reinterpret_cast<u32*>(dst)[i] = i;
    }
}
namespace vm { std::pair<u32, bool> try_get_addr(void*) { return {0x10000, true}; } }
namespace fmt { [[noreturn]] void throw_exception(const char* s) { throw std::runtime_error(s); } }
struct { struct { bool multithreaded_rsx = true; bool strict_rendering_mode = true; } video;
         struct { thread_scheduler_mode thread_scheduler = thread_scheduler_mode::os; } core; } g_cfg;
#include "production.h"
template<class T> class named_thread : public T, public thread_base
{
public:
    operator thread_state() const { return this->status.load(); }
    void operator=(thread_state s) { this->status = s; }
};
#include "production.inc"

using worker = named_thread<rsx::dma_manager::offload_thread>;
std::thread start_worker(worker& w)
{
    return std::thread([&] { thread_ctrl::current = &w; w(); });
}
int main()
{
    thread_ctrl::current = &rsx::gpu;
    rsx::dma_manager d;
    assert(d.sync(true)); d.join(); // Stop before the worker exists.
    d.m_thread = std::make_shared<worker>();
    auto& w = *d.m_thread;

    // A parked worker is deliberately never started. Only the FIFO thread
    // can execute this batch; normal submission still defaults to waiting.
    std::array<u8, 8192> src{}, dst{};
    for (unsigned i = 0; i < src.size(); ++i) src[i] = i;
    d.copy(dst.data(), src.data(), src.size());
    assert(d.sync(true)); assert(dst == src); assert(rsx::reservations == 1);
    std::vector<u8> vector(8192, 0x5a);
    d.copy(dst.data(), vector, vector.size());
    assert(d.sync(true)); assert(dst[8191] == 0x5a);
    std::array<u32, 4096> indices{};
    d.emulate_as_indexed(indices.data(), rsx::primitive_type::quads, indices.size());
    assert(d.sync(true)); for (unsigned i = 0; i < indices.size(); ++i) assert(indices[i] == i);

    unsigned calls = 0;
    rsx::gpu.callback = [&](u32 cmd, void*)
    {
        assert(!d.is_current_thread()); // Inline faults use RSX recovery, not offloader recovery.
        assert(!d.sync()); // Command-queue flush re-entry must return, never deadlock.
        assert(!d.sync(true));
        ++calls;
        if (cmd == 1) d.backend_ctrl(2, nullptr); // Work added during the batch follows it.
    };
    d.backend_ctrl(1, nullptr); assert(d.sync(true)); assert(calls == 2);
    assert(w.m_exec_claim.load() == 0 && !w.m_rsx_running_jobs);

    rsx::gpu.callback = [&](u32, void*) { throw std::runtime_error("injected callback failure"); };
    d.backend_ctrl(0, nullptr);
    try { d.sync(true); assert(false); } catch (const std::runtime_error&) {}
    assert(w.m_exec_claim.load() == 0 && !w.m_rsx_running_jobs);
    w.m_processed_count = w.m_enqueued_count.load(); // A fatal job terminates its real renderer.

    // A held worker claim may never be stolen. It completes the first batch,
    // releases ownership, then the FIFO thread can drain the remaining queue.
    w.m_exec_claim = 1;
    d.backend_ctrl(0, nullptr);
    rsx::gpu.callback = [&](u32, void*) { ++calls; };
    std::thread release([&] { std::this_thread::sleep_for(std::chrono::milliseconds(2)); w.m_exec_claim = 0; });
    assert(d.sync(true)); release.join();

    // Ordinary command submission, and callers on other threads, must use
    // the helper even if run_here is requested. Both are exercised live.
    std::atomic<bool> on_worker{false};
    rsx::gpu.callback = [&](u32, void*) { on_worker = !rsx::gpu.is_current_thread(); assert(d.is_current_thread()); };
    auto background = start_worker(w);
    d.backend_ctrl(0, nullptr); assert(d.sync()); assert(on_worker);
    on_worker = false;
    d.backend_ctrl(0, nullptr);
    std::thread other([&] { assert(d.sync(true)); }); other.join(); assert(on_worker);

    // Contending consumers preserve order and exactly-once execution across
    // 40,000 callbacks and arbitrarily split pop_all batches.
    std::vector<u32> seen;
    seen.reserve(40'000);
    std::atomic<unsigned> inside{0};
    rsx::gpu.callback = [&](u32 n, void*)
    {
        assert(inside.fetch_add(1) == 0);
        seen.push_back(n);
        assert(inside.fetch_sub(1) == 1);
    };
    for (u32 i = 0; i < 40'000; ++i)
    {
        d.backend_ctrl(i, nullptr);
        if (i % 7 == 0) assert(d.sync(true));
    }
    assert(d.sync(true)); assert(seen.size() == 40'000);
    for (u32 i = 0; i < seen.size(); ++i) assert(seen[i] == i);
    d.join(); background.join(); d.join();
    assert(w.m_exec_claim.load() == 0 && w.m_current_job == nullptr);
    std::puts("RSX queue: parked handoff, four jobs, recursion, failure cleanup, ownership, submission, ordering and teardown passed.");
}
