#include "../IOSGPUEventWait.h"

#include <cassert>
#include <cstdio>
#include <initializer_list>

enum class status { reset, set, error, timeout };

int main()
{
    // Sweep completion on either side of every poll/counter/wait boundary.
    unsigned cases = 0;
    for (unsigned completion = 0; completion < 10000; ++completion)
    {
        for (unsigned race = 0; race < 3; ++race)
        {
            std::uint64_t now = 0, counter = 0;
            unsigned operations = 0;
            bool ready = false;
            auto signal = [&]
            {
                if (!ready && ++operations >= completion)
                {
                    ready = true;
                    ++counter;
                }
            };
            auto result = rpcs3::ios::wait_for_gpu_event(status::reset, status::timeout, 0,
                [&] { if (race == 0) signal(); return counter; },
                [&] { if (race == 1) signal(); return ready ? status::set : status::reset; },
                [&] { return now++; },
                [&](std::uint64_t value, std::uint64_t ms)
                {
                    assert(ms == 1);
                    if (race == 2) signal();
                    // If completion raced with entering wait, the target is
                    // already satisfied. Never wait for a second completion.
                    if (ready) assert(value <= counter);
                    else now += 1000;
                },
                [&] { if (race == 2) signal(); });
            assert(result == status::set && ready);
            ++cases;
        }
    }

    for (std::uint64_t timeout : {1, 19, 20, 999, 1000, 1020, 10000})
    {
        std::uint64_t now = 0;
        const auto result = rpcs3::ios::wait_for_gpu_event(status::reset, status::timeout, timeout,
            [] { return std::uint64_t{0}; }, [] { return status::reset; }, [&] { return now; },
            [&](std::uint64_t value, std::uint64_t ms)
            {
                assert(value == 1 && ms == 1 && timeout - now >= 1000);
                now += 1000;
            }, [&] { ++now; });
        assert(result == status::timeout && now == timeout);
        ++cases;
    }

    // Errors, spurious wakes, multiple resets/reuses, and saturated counters.
    for (auto initial : {status::set, status::error})
    {
        const auto result = rpcs3::ios::wait_for_gpu_event(status::reset, status::timeout, 1,
            [] { return std::uint64_t{0}; }, [=] { return initial; }, [] { return 0; },
            [](auto, auto) { assert(false); }, [] { assert(false); });
        assert(result == initial);
        ++cases;
    }
    for (std::uint64_t base : {0ull, 2ull, 100ull, ~0ull})
    {
        std::uint64_t now = 0;
        unsigned wakes = 0;
        const auto result = rpcs3::ios::wait_for_gpu_event(status::reset, status::timeout, 10000,
            [=] { return base; }, [&] { return now >= 2000 ? status::error : status::reset; },
            [&] { return now; },
            [&](std::uint64_t value, auto) { assert(value == base + 1); now += 100; ++wakes; },
            [&] { ++now; });
        assert(result == status::error);
        assert(base == ~0ull ? wakes == 0 : wakes > 1);
        ++cases;
    }
    std::printf("%u GPU event wait cases passed\n", cases);
}
