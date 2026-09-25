#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

extern "C" void baseline(const std::uint32_t*, std::uint64_t*, std::uint64_t);
extern "C" void candidate(const std::uint32_t*, std::uint64_t*, std::uint64_t);
using converter = decltype(&baseline);
using clock_type = std::chrono::steady_clock;

// Independent field construction: sign, biased exponent and 23-bit fraction.
std::uint64_t reference(std::uint32_t x)
{
    const std::uint64_t sign = std::uint64_t(x >> 31) << 63;
    const auto exponent = (x >> 23) & 255;
    if (!exponent) return sign;
    return sign | (std::uint64_t(exponent + 896) << 52)
        | (std::uint64_t(x & 0x7fffff) << 29);
}

int main(int argc, char**)
{
    constexpr std::size_t count = 4096;
    std::array<std::uint32_t, count> input;
    std::array<std::uint64_t, count> old_result, new_result;
    const std::uint64_t limit = argc > 1 ? (1ull << 32) : (1ull << 22);
    auto start = clock_type::now();
    for (std::uint64_t base = 0; base < limit; base += count)
    {
        // Odd multiplication permutes all 32-bit values, mixes signs/exponents
        // across lanes, and still visits every input in exhaustive mode.
        for (std::size_t i = 0; i < count; ++i)
            input[i] = std::uint32_t(base + i) * 0x9e3779b1u;
        baseline(input.data(), old_result.data(), count);
        candidate(input.data(), new_result.data(), count);
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto expected = reference(input[i]);
            if (old_result[i] != expected || new_result[i] != expected)
            {
                std::fprintf(stderr, "Mismatch x=%08x old=%016llx new=%016llx expected=%016llx\n",
                    input[i], static_cast<unsigned long long>(old_result[i]),
                    static_cast<unsigned long long>(new_result[i]), static_cast<unsigned long long>(expected));
                return 1;
            }
        }
    }
    std::printf("PASS: %llu input bit patterns, baseline == candidate == independent reference (%.3f s)\n",
        static_cast<unsigned long long>(limit), std::chrono::duration<double>(clock_type::now() - start).count());
    constexpr int repeats = 10000;
    std::array<std::vector<double>, 2> timings;
    for (int round = 0; round < 12; ++round)
    {
        for (int order = 0; order < 2; ++order)
        {
            const int which = order ^ (round & 1);
            converter convert = which ? candidate : baseline;
            auto begin = clock_type::now();
            for (int n = 0; n < repeats; ++n) convert(input.data(), new_result.data(), count);
            const double ns = std::chrono::duration<double, std::nano>(clock_type::now() - begin).count() / (repeats * count);
            if (round >= 2) timings[which].push_back(ns);
        }
    }
    double medians[2];
    for (int i = 0; i < 2; ++i)
    {
        auto& times = timings[i]; std::sort(times.begin(), times.end());
        medians[i] = (times[4] + times[5]) / 2;
        std::printf("%s: median %.6f ns/lane, range %.6f..%.6f, 10 alternating measured rounds\n",
            i ? "candidate" : "baseline", medians[i], times.front(), times.back());
    }
    std::printf("Conversion throughput improvement: %.2f%%; elapsed cost reduction: %.2f%%\n",
        100 * (medians[0] / medians[1] - 1), 100 * (1 - medians[1] / medians[0]));
}
