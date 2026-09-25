#include <algorithm>
#include <array>
#include <chrono>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" {
void pack_baseline(const std::uint64_t*, std::uint32_t*, std::uint64_t);
void pack_candidate(const std::uint64_t*, std::uint32_t*, std::uint64_t);
void mask_baseline(const std::uint32_t*, std::uint64_t*, std::uint64_t);
void mask_candidate(const std::uint32_t*, std::uint64_t*, std::uint64_t);
void gt_baseline(const std::uint32_t*, const std::uint32_t*, std::uint32_t*, std::uint64_t);
void gt_candidate(const std::uint32_t*, const std::uint32_t*, std::uint32_t*, std::uint64_t);
void eq_baseline(const std::uint32_t*, const std::uint32_t*, std::uint32_t*, std::uint64_t);
void eq_candidate(const std::uint32_t*, const std::uint32_t*, std::uint32_t*, std::uint64_t);
void mgt_baseline(const std::uint32_t*, const std::uint32_t*, std::uint32_t*, std::uint64_t);
void mgt_candidate(const std::uint32_t*, const std::uint32_t*, std::uint32_t*, std::uint64_t);
void meq_baseline(const std::uint32_t*, const std::uint32_t*, std::uint32_t*, std::uint64_t);
void meq_candidate(const std::uint32_t*, const std::uint32_t*, std::uint32_t*, std::uint64_t);
void insert_baseline(const std::uint8_t*, const std::uint8_t*, const std::uint32_t*, std::uint8_t*, std::uint64_t);
void insert_candidate(const std::uint8_t*, const std::uint8_t*, const std::uint32_t*, std::uint8_t*, std::uint64_t);
}

using u8 = std::uint8_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using timer = std::chrono::steady_clock;
constexpr std::size_t count = 4096;

[[noreturn]] void failure(const char* name, u64 input, u64 old, u64 candidate, u64 expected)
{
    std::fprintf(stderr, "%s mismatch input=%016llx old=%016llx candidate=%016llx expected=%016llx\n", name,
        static_cast<unsigned long long>(input), static_cast<unsigned long long>(old),
        static_cast<unsigned long long>(candidate), static_cast<unsigned long long>(expected));
    std::exit(1);
}

u32 pack_reference(u64 d)
{
    if (!d) return 0;
    const u32 fraction_and_exponent = u32(d >> 29) & 0x3fffffffu;
    const u32 rebased_exponent = (d & (1ull << 59)) ? 0 : 0x40000000u;
    const u32 sign = d >> 63 ? 0x80000000u : 0;
    return fraction_and_exponent | rebased_exponent | sign;
}

double xfloat_reference(u32 raw)
{
    const u64 sign = u64(raw >> 31) << 63;
    const u32 exponent = (raw >> 23) & 255;
    const u64 d = !exponent ? sign : sign | (u64(exponent + 896) << 52) | (u64(raw & 0x7fffff) << 29);
    return std::bit_cast<double>(d);
}

u64 mask_reference(u32 x)
{
    // Bit 31 is the double sign. Bit 30 fills the extended exponent.
    return (u64(x >> 31) << 63) | (u64(x & 0x7fffffffu) << 28)
        | ((x & 0x40000000u) ? 0x7800000000000000ull : 0);
}

template <typename Work>
void measure(const char* name, Work work, double operations)
{
    std::array<std::vector<double>, 2> samples;
    for (int round = 0; round < 12; ++round)
    {
        for (int order = 0; order < 2; ++order)
        {
            const int which = order ^ (round & 1);
            const auto begin = timer::now();
            work(which);
            const double ns = std::chrono::duration<double, std::nano>(timer::now() - begin).count() / operations;
            if (round >= 2) samples[which].push_back(ns);
        }
    }
    double medians[2];
    for (int i = 0; i < 2; ++i)
    {
        auto& values = samples[i]; std::sort(values.begin(), values.end());
        medians[i] = (values[4] + values[5]) * .5;
        std::printf("%s,%s,%.6f,%.6f,%.6f\n", name, i ? "candidate" : "baseline", medians[i], values.front(), values.back());
    }
    std::printf("%s cost reduction %.2f%%\n", name, 100 * (1 - medians[1] / medians[0]));
}

int main(int argc, char** argv)
{
    const bool exhaustive = argc > 1 && std::strcmp(argv[1], "exhaustive") == 0;
    const u64 limit = exhaustive ? (1ull << 32) : (1ull << 22);
    std::array<u32, count> input32, old32, new32;
    std::array<u64, count> input64, old64, new64;
    auto start = timer::now();

    for (u64 base = 0; base < limit; base += count)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            const u32 x = u32(base + i) * 0x9e3779b1u;
            input32[i] = x;
            input64[i] = (u64(x & 0x7fffffffu) << 29) | (u64(x >> 31) << 63);
        }
        pack_baseline(input64.data(), old32.data(), count); pack_candidate(input64.data(), new32.data(), count);
        mask_baseline(input32.data(), old64.data(), count); mask_candidate(input32.data(), new64.data(), count);
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto p = pack_reference(input64[i]); const auto m = mask_reference(input32[i]);
            if (p != old32[i] || p != new32[i]) failure("pack", input64[i], old32[i], new32[i], p);
            if (m != old64[i] || m != new64[i]) failure("mask", input32[i], old64[i], new64[i], m);
        }
    }
    // Packing ignores these bits except when they distinguish a nonzero value
    // from positive zero. Test that exception and signed zero explicitly.
    for (u64 ignored : {1ull, 0x1fffffffull, 1ull << 60, 1ull << 61, 1ull << 62, 1ull << 63})
    {
        input64.fill(ignored); pack_baseline(input64.data(), old32.data(), count); pack_candidate(input64.data(), new32.data(), count);
        for (std::size_t i = 0; i < count; ++i)
            if (old32[i] != pack_reference(ignored) || new32[i] != pack_reference(ignored))
                failure("pack-zero", ignored, old32[i], new32[i], pack_reference(ignored));
    }
    std::printf("PASS pack/mask: %llu meaningful input combinations each (%.3f s)\n",
        static_cast<unsigned long long>(limit), std::chrono::duration<double>(timer::now() - start).count()); std::fflush(stdout);

    using compare = decltype(&gt_baseline);
    const std::array<std::array<compare, 2>, 4> functions = {{{gt_baseline, gt_candidate}, {eq_baseline, eq_candidate}, {mgt_baseline, mgt_candidate}, {meq_baseline, meq_candidate}}};
    const std::array<const char*, 4> names = {"gt", "eq", "mgt", "meq"};
    std::array<u32, count> lhs, rhs;
    const auto check_comparisons = [&]()
    {
        for (u32 op = 0; op < 4; ++op)
        {
            functions[op][0](lhs.data(), rhs.data(), old32.data(), count);
            functions[op][1](lhs.data(), rhs.data(), new32.data(), count);
            for (std::size_t i = 0; i < count; ++i)
            {
                double a = xfloat_reference(lhs[i]), b = xfloat_reference(rhs[i]);
                if (op >= 2) { a = std::abs(a); b = std::abs(b); }
                const u32 expected = ((op & 1) ? a == b : a > b) ? 0xffffffffu : 0;
                if (old32[i] != expected || new32[i] != expected)
                    failure(names[op], (u64(lhs[i]) << 32) | rhs[i], old32[i], new32[i], expected);
            }
        }
    };
    // Every sign/exponent against every sign/exponent at representative
    // mantissa boundaries, plus equal, opposite-sign and unrelated pairs.
    constexpr std::array<u32, 6> fractions = {0, 1, 2, 0x3fffff, 0x7ffffe, 0x7fffff};
    std::size_t used = 0;
    for (u32 a = 0; a < 512; ++a) for (u32 b = 0; b < 512; ++b)
        for (u32 af : fractions) for (u32 bf : fractions)
        {
            lhs[used] = (a << 23) | af; rhs[used] = (b << 23) | bf;
            if (++used == count) { check_comparisons(); used = 0; }
        }
    if (used) std::abort();
    for (u64 base = 0; base < limit; base += count)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            const u32 x = u32(base + i) * 0x9e3779b1u;
            lhs[i] = x;
            switch (i & 3)
            {
            case 0: rhs[i] = x; break;
            case 1: rhs[i] = x ^ 0x80000000u; break;
            case 2: rhs[i] = 0; break;
            case 3: rhs[i] = (x * 0x85ebca6bu) ^ 0xa5a5a5a5u; break;
            }
        }
        check_comparisons();
    }
    std::printf("PASS comparisons: 9437184 sign/exponent/mantissa-boundary pairs plus %llu permuted pairs, each for all four predicates\n", static_cast<unsigned long long>(limit));
    std::fflush(stdout);

    std::array<u8, 16> bytes, before, after, expected;
    std::array<u8, count> elements;
    std::array<u32, count> indices;
    for (u32 seed = 0; seed < 256; ++seed)
    {
        for (u32 lane = 0; lane < 16; ++lane)
            for (u32 element = 0; element < 256; ++element)
            {
                for (u32 i = 0; i < 16; ++i) bytes[i] = u8(seed + i * 17);
                expected = bytes; expected[lane] = u8(element); elements[0] = u8(element); indices[0] = lane;
                insert_baseline(bytes.data(), elements.data(), indices.data(), before.data(), 1);
                insert_candidate(bytes.data(), elements.data(), indices.data(), after.data(), 1);
                if (before != expected || after != expected) failure("insert", lane, before[lane], after[lane], element);
            }
    }
    for (std::size_t i = 0; i < count; ++i) { elements[i] = u8(i * 117); indices[i] = u32(i * 13) & 15; }
    expected = bytes;
    for (std::size_t i = 0; i < count; ++i) expected[indices[i]] = elements[i];
    insert_baseline(bytes.data(), elements.data(), indices.data(), before.data(), count);
    insert_candidate(bytes.data(), elements.data(), indices.data(), after.data(), count);
    if (before != expected || after != expected) failure("insert-chain", 0, 0, 0, 1);
    std::puts("PASS insert: 1048576 lane/value/base cases and a 4096-update dependency chain");

    if (argc > 2 && std::strcmp(argv[2], "validate") == 0) return 0;
    for (std::size_t i = 0; i < count; ++i)
        input64[i] = (u64(input32[i]) << 32) | input32[(i + 1) % count];

    constexpr int repeats = 10000;
    std::puts("case,variant,median_ns_per_value,min,max (10 alternating measured rounds; 2 warmups)");
    measure("pack", [&](int n) { auto fn = n ? pack_candidate : pack_baseline; for (int i = 0; i < repeats; ++i) fn(input64.data(), new32.data(), count); }, double(repeats) * count);
    measure("mask", [&](int n) { auto fn = n ? mask_candidate : mask_baseline; for (int i = 0; i < repeats; ++i) fn(input32.data(), new64.data(), count); }, double(repeats) * count);
    for (u32 op = 0; op < 4; ++op)
        measure(names[op], [&](int n) { auto fn = functions[op][n]; for (int i = 0; i < repeats; ++i) fn(lhs.data(), rhs.data(), new32.data(), count); }, double(repeats) * count);
    measure("insert", [&](int n) { auto fn = n ? insert_candidate : insert_baseline; for (int i = 0; i < repeats; ++i) fn(bytes.data(), elements.data(), indices.data(), after.data(), count); }, double(repeats) * count);
}
