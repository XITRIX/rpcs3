#include <atomic>
#include <cassert>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <sys/mman.h>
#include <unistd.h>

using u32 = std::uint32_t;
using s32 = std::int32_t;
#define ensure(value) assert(value)
template <typename T> using atomic_t = std::atomic<T>;
using shared_mutex = std::shared_mutex;

struct reader_lock
{
    shared_mutex& mutex;
    bool exclusive = false;
    explicit reader_lock(shared_mutex& value) : mutex(value) { mutex.lock_shared(); }
    void upgrade() { mutex.unlock_shared(); mutex.lock(); exclusive = true; }
    ~reader_lock() { if (exclusive) mutex.unlock(); else mutex.unlock_shared(); }
};

namespace utils
{
    enum class protection { no, rw };
    std::size_t get_page_size() { return static_cast<std::size_t>(sysconf(_SC_PAGESIZE)); }
    u32 page_start(u32 value) { return value & -static_cast<u32>(get_page_size()); }
    void memory_protect(void* pointer, std::size_t size, protection value)
    {
        assert(mprotect(pointer, size, value == protection::rw ? PROT_READ | PROT_WRITE : PROT_NONE) == 0);
    }
}
namespace vm
{
    using addr_t = u32;
    u32 guest_page;
    std::uint8_t* host_page;
    void* base(u32 address)
    {
        assert(address >= guest_page && address - guest_page < utils::get_page_size());
        return host_page + (address - guest_page);
    }
}
struct { template <typename... T> void warning(const char*, T...) {} } rsx_log;

namespace rsx
{
    constexpr u32 pipe_flush_interrupt = 1;
    u32 classify_location(u32 address) { return address >= 0xc0000000 ? 0 : 1; }
    const char* location_tostring(u32) { return "test report location"; }
    struct thread
    {
        std::thread::id owner = std::this_thread::get_id();
        std::atomic<unsigned> pause_requests{0};
        std::atomic<bool> pause_ack{false};
        std::atomic<unsigned> m_eng_interrupt_mask{0};
        bool is_current_thread() const { return owner == std::this_thread::get_id(); }
        void pause()
        {
            ++pause_requests;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (!pause_ack.load())
            {
                if (std::chrono::steady_clock::now() >= deadline) std::_Exit(87);
                std::this_thread::yield();
            }
        }
        void unpause() { --pause_requests; }
    } renderer;
    thread* get_current_renderer() { return &renderer; }
    struct eng_lock
    {
        thread* target;
        explicit eng_lock(thread* value) : target(value->is_current_thread() ? nullptr : value)
        {
            if (target) target->pause();
        }
        ~eng_lock() { if (target) target->unpause(); }
    };
    namespace reports
    {
        struct MMIO_page_data_t
        {
            unsigned refs = 0;
            utils::protection prot = utils::protection::rw;
            void add_ref() { ++refs; }
            bool has_refs() const { return refs != 0; }
            void release() { assert(refs); --refs; }
        };
        struct ZCULL_control
        {
            std::unordered_map<u32, MMIO_page_data_t> m_locked_pages[2];
            atomic_t<bool> m_pages_accessed[2]{false, false};
            atomic_t<s32> m_critical_reports_in_flight{0};
            shared_mutex m_pages_mutex;
            void on_report_enqueued(vm::addr_t);
            void on_report_completed(vm::addr_t);
            void disable_optimizations(rsx::thread*, u32);
            bool on_access_violation(u32);
        };
#include "production.inc"
    }
}

rsx::reports::ZCULL_control* active_controller;
std::atomic<unsigned> native_faults{0};
void fault_handler(int, siginfo_t* info, void*)
{
    const auto offset = reinterpret_cast<std::uintptr_t>(info->si_addr) -
        reinterpret_cast<std::uintptr_t>(vm::host_page);
    if (offset >= utils::get_page_size()) std::_Exit(88);
    ++native_faults;
    if (!active_controller->on_access_violation(vm::guest_page + static_cast<u32>(offset)))
    {
        // Production's outer VM handler retries an otherwise valid guest read.
        // Refusing this renderer fault while a producer waits for its pause
        // leaves the page inaccessible and the renderer unable to acknowledge.
        if (rsx::renderer.is_current_thread() && rsx::renderer.pause_requests == 1)
            std::_Exit(86);
        std::_Exit(88);
    }
}

int main()
{
    struct sigaction action{};
    action.sa_sigaction = fault_handler;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    assert(sigaction(SIGSEGV, &action, nullptr) == 0);
    assert(sigaction(SIGBUS, &action, nullptr) == 0);
    vm::host_page = static_cast<std::uint8_t*>(mmap(nullptr, utils::get_page_size(),
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
    assert(vm::host_page != MAP_FAILED);

    for (u32 guest_page : {0x50300000u, 0xc0300000u})
    {
        vm::guest_page = guest_page;
        const u32 location = rsx::classify_location(guest_page);
        auto* report = reinterpret_cast<volatile u32*>(vm::host_page + 0x440);
        auto* semaphore = reinterpret_cast<volatile u32*>(vm::host_page + 0x430);
        *report = 0x12345678;
        *semaphore = 0x87654321;

        // Stale protected pages still recover without disabling the location.
        {
            rsx::reports::ZCULL_control controller;
            controller.on_report_enqueued(guest_page + 0x440);
            controller.on_report_completed(guest_page + 0x440);
            assert(controller.on_access_violation(guest_page + 0x440));
            assert(!controller.m_pages_accessed[location]);
            assert(controller.m_locked_pages[location].empty());
            assert(*report == 0x12345678);
            assert(!controller.on_access_violation(guest_page + utils::get_page_size()));
        }

        rsx::reports::ZCULL_control controller;
        active_controller = &controller;
        native_faults = 0;
        rsx::renderer.pause_ack = false;
        controller.on_report_enqueued(guest_page + 0x440);

        // Exact observed cycle: the SPU faults on a referenced report and
        // waits for FIFO pause. Before reaching that pause point, the renderer
        // reads a semaphore on the same native protected page.
        u32 observed = 0;
        std::thread producer([&] { observed = *report; });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (rsx::renderer.pause_requests == 0)
        {
            assert(std::chrono::steady_clock::now() < deadline);
            std::this_thread::yield();
        }
        assert(*semaphore == 0x87654321);
        rsx::renderer.pause_ack = true;
        producer.join();
        assert(observed == 0x12345678 && native_faults == 2);
        assert(rsx::renderer.pause_requests == 0);
        assert(controller.m_pages_accessed[location]);
        assert(controller.m_locked_pages[location].empty());
        assert(controller.m_critical_reports_in_flight == 1);
        controller.on_report_completed(guest_page + 0x440);
        assert(controller.m_critical_reports_in_flight == 0);
        controller.on_report_enqueued(guest_page + 0x440);
        assert(controller.m_critical_reports_in_flight == 1);
        controller.on_report_completed(guest_page + 0x440);
        assert(controller.m_critical_reports_in_flight == 0);
    }
    assert(munmap(vm::host_page, utils::get_page_size()) == 0);
    std::printf("ZCULL: concurrent native report/semaphore faults recover on %zu-byte pages; stale pages and report accounting pass.\n", utils::get_page_size());
}
