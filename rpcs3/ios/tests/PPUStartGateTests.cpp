#include "Utilities/bit_set.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <thread>
#include "CPUFlags.inc"

struct ppu_thread
{
    atomic_bs_t<cpu_flag> state{};
    atomic_t<ppu_join_status> joiner{ppu_join_status::joinable};
    atomic_t<u32> start_gate_caller{0};
    bool is_stopped() const { return ::is_stopped(+state); }
    void entry_gate();
};
template <typename T> using named_thread = T;
static std::shared_ptr<ppu_thread> creator;
static u64 now_us;
static unsigned clock_reads;
static unsigned zombie_at_read;
static unsigned child_stop_at_read;
static unsigned child_suspend_at_read;
static ppu_thread* child_under_test;
u64 get_system_time()
{
    ++clock_reads;
    if (clock_reads == zombie_at_read)
        creator->joiner = ppu_join_status::zombie;
    if (clock_reads == child_stop_at_read)
        child_under_test->state += cpu_flag::exit;
    if (clock_reads == child_suspend_at_read)
        child_under_test->state += cpu_flag::suspend;
    now_us += 1;
    return now_us;
}
namespace idm
{
    template <typename T> std::shared_ptr<T> get_unlocked(u32 id)
    {
        assert(id == 0x1000009);
        return creator;
    }
}
void ppu_thread::entry_gate()
{
#include "PPUStartGateUnderTest.inc"
}

static void run(bs_t<cpu_flag> state, ppu_join_status joiner, bool bounded = false,
                unsigned zombie_transition = 0, unsigned child_stop = 0,
                bool initially_suspended = false, unsigned child_suspend = 0)
{
    creator = std::make_shared<ppu_thread>();
    creator->state.store(state);
    creator->joiner = joiner;
    ppu_thread child;
    child.start_gate_caller = 0x1000009;
    if (initially_suspended) child.state += cpu_flag::suspend;
    child_under_test = &child;
    now_us = 0;
    clock_reads = 0;
    zombie_at_read = zombie_transition;
    child_stop_at_read = child_stop;
    child_suspend_at_read = child_suspend;
    child.entry_gate();
    assert(child.start_gate_caller == 0);
    assert(now_us <= 1020);
    if (bounded) assert(now_us >= 1000);
    else assert(now_us <= 500);
    if (initially_suspended) assert(clock_reads <= 1);
    if (child_suspend) assert(clock_reads <= child_suspend + 1);
}
int main()
{
    // Physical capture: trophy_init_thread has state 516 (wait + memory),
    // zombie join status and no suspend/exit. Its child must proceed to guest code.
    run(cpu_flag::wait + cpu_flag::memory, ppu_join_status::zombie);
    run(+cpu_flag::wait, ppu_join_status::exited);
    run(cpu_flag::wait + cpu_flag::suspend, ppu_join_status::joinable);
    run(cpu_flag::wait + cpu_flag::exit, ppu_join_status::joinable);
    run(cpu_flag::wait + cpu_flag::stop, ppu_join_status::joinable);
    run(cpu_flag::wait + cpu_flag::req_exit, ppu_join_status::joinable);
    run({}, ppu_join_status::joinable); // Caller left the syscall: retain its head start.
    run(+cpu_flag::wait, ppu_join_status::joinable, true); // Missed transient wait clear.
    run(+cpu_flag::wait, ppu_join_status::detached, true);
    run(+cpu_flag::wait, static_cast<ppu_join_status>(0x1000000), true); // Active joiner ID.
    run(+cpu_flag::wait, ppu_join_status::joinable, false, 3);
    run(+cpu_flag::wait, ppu_join_status::joinable, false, 0, 3);
    // A child cannot acknowledge suspension while spinning inside entry_call.
    run(+cpu_flag::wait, ppu_join_status::joinable, false, 0, 0, true);
    run({}, ppu_join_status::joinable, false, 0, 0, true);
    run(+cpu_flag::wait, ppu_join_status::joinable, false, 0, 0, false, 3);
    run({}, ppu_join_status::joinable, false, 0, 0, false, 3); // Inner head-start loop.
    creator.reset();
    ppu_thread child;
    child.start_gate_caller = 0x1000009;
    child.entry_gate(); // Creator already removed from IDM.
    assert(child.start_gate_caller == 0);
    child.entry_gate(); // No gate is also valid.
    std::puts("PPU startup gate: 18 production cases passed");
}
