#pragma once

#include <cstdint>
#include <limits>

namespace rpcs3::ios
{
// Poll briefly for an already finishing transfer, then wait for the native
// completion counter to advance. A wake is only a hint: the Vulkan status is
// authoritative. Read the counter BEFORE checking status to avoid losing a
// signal between the status check and entering the native wait.
template <typename Status, typename Counter, typename Poll, typename Clock,
    typename Wait, typename Pause>
Status wait_for_gpu_event(Status pending, Status timed_out,
    std::uint64_t timeout_us, Counter counter, Poll poll, Clock clock,
    Wait wait, Pause pause)
{
    const auto start = clock();
    for (;;)
    {
        const auto observed = counter();
        const auto status = poll();
        if (status != pending)
        {
            return status;
        }

        const auto elapsed = clock() - start;
        if (timeout_us && elapsed >= timeout_us)
        {
            return timed_out;
        }

        // One millisecond bounds error/timeout rechecks, not completion latency:
        // Metal wakes the waiter immediately when the GPU signals the event.
        // Poll the final sub-millisecond timeout tail instead of rounding it up.
        if (elapsed >= 20 && (!timeout_us || timeout_us - elapsed >= 1000) &&
            observed != std::numeric_limits<std::uint64_t>::max())
        {
            wait(observed + 1, 1);
        }
        else
        {
            pause();
        }
    }
}
}
