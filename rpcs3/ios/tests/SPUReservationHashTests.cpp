#include "Emu/CPU/Backends/AArch64/SPUReservationHash.h"
#include "util/v128.hpp"
#include "Utilities/StrFmt.h"
#include "util/simd.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>
#include "SPUReservationHashFixture.h"

// simd.hpp's unrelated global capability state requires these symbols. Neither
// is called by the tested ARM64 arithmetic or its timing loops.
namespace utils
{
bool has_avx() { return false; }
u64 _get_main_tid() { return 0; }
}

__attribute__((noinline)) u32 candidate(const void* data) { return aarch64::spu_rdata_hash32(data); }
u32 oracle(const void* data)
{
	u32 sum = 0;
	for (unsigned i = 0; i < 128; i += 4)
	{
		u32 word;
		std::memcpy(&word, static_cast<const unsigned char*>(data) + i, 4);
		sum += word;
	}
	return sum;
}
unsigned checks = 0;
void check(const void* data)
{
	const auto expected = oracle(data);
	if (candidate(data) != expected || (!(reinterpret_cast<std::uintptr_t>(data) & 15) && baseline(data) != expected))
		std::abort();
	++checks;
}
volatile u32 hash_sink;
using hash_fn = u32(*)(const void*);
template <bool Dependent>
__attribute__((noinline)) double measure(hash_fn fn, const unsigned char* data, unsigned count)
{
	u32 state = 0;
	const auto start = std::chrono::steady_clock::now();
	for (unsigned i = 0; i < 2000000; ++i)
	{
		asm volatile("" ::: "memory");
		const unsigned index = (Dependent ? i ^ state : i) & (count - 1);
		state = fn(data + index * 128);
	}
	hash_sink = state;
	return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / 2000000;
}
int main(int argc, char**)
{
	alignas(128) std::array<unsigned char, 160> data{};
	for (unsigned offset = 0; offset < 16; ++offset)
	{
		data.fill(0); check(data.data() + offset);
		data.fill(255); check(data.data() + offset);
		for (unsigned bit = 0; bit < 1024; ++bit)
		{
			data.fill(0);
			data[offset + bit / 8] = 1u << (bit % 8);
			check(data.data() + offset);
		}
	}
	std::mt19937_64 rng(242);
	for (unsigned i = 0; i < 200000; ++i)
	{
		for (auto& byte : data) byte = static_cast<unsigned char>(rng());
		check(data.data() + i % 16);
	}
	const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
	auto* mapped = static_cast<unsigned char*>(mmap(nullptr, page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0));
	if (mapped == MAP_FAILED || mprotect(mapped + page, page, PROT_READ | PROT_WRITE)) std::abort();
	for (auto offset : {page, page * 2 - 128})
	{
		for (unsigned i = 0; i < 128; ++i) mapped[offset + i] = static_cast<unsigned char>(rng());
		check(mapped + offset);
	}
	munmap(mapped, page * 3);
	std::printf("PASS reservation hash: %u scalar-oracle checks\n", checks);
	if (argc == 1) return 0;
	for (unsigned count : {1u, 256u, 16384u}) for (bool dependent : {false, true})
	{
		std::vector<unsigned char> input(count * 128);
		for (auto& byte : input) byte = static_cast<unsigned char>(rng());
		std::array<std::vector<double>, 2> times;
		for (unsigned round = 0; round < 12; ++round) for (unsigned order = 0; order < 2; ++order)
		{
			const auto version = (round + order) % 2;
			const auto fn = version ? candidate : baseline;
			const auto ns = dependent ? measure<true>(fn, input.data(), count) : measure<false>(fn, input.data(), count);
			if (round >= 2) times[version].push_back(ns);
		}
		for (auto& time : times) std::sort(time.begin(), time.end());
		const auto before = (times[0][4] + times[0][5]) / 2;
		const auto after = (times[1][4] + times[1][5]) / 2;
		std::printf("entries=%u dependent=%u baseline=%.3f candidate=%.3f ns saved=%.2f%%\n", count, dependent, before, after, 100 * (1 - after / before));
	}
}
