#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>
using u64 = std::uint64_t;
using u32 = std::uint32_t;
struct alignas(16) v128
{
	union
	{
		u64 _u64[2];
		u32 _u32[4];
	};
};
#include "FragmentConstantFixture.h"
template <bool New>
__attribute__((noinline)) u64 hash(const v128* p, unsigned n)
{
	u64 a = 0, b = 0;
	for (unsigned i = 0; i < n; ++i)
	{
		v128 x = p[i];
		a += std::rotr(x._u64[0], i * 2);
		b += std::rotr(x._u64[1], i * 2 + 1);
		if (New ? new_test(x) : old_test(x))
			++i;
	}
	return a + b;
}
template <bool New>
__attribute__((noinline)) bool compare(const v128* a, const v128* b, unsigned n)
{
	for (unsigned i = 0; i < n; ++i)
	{
		auto x = a[i];
		auto y = b[i];
		if ((x._u64[0] ^ y._u64[0]) | (x._u64[1] ^ y._u64[1]))
			return false;
		if (New ? new_test(x) : old_test(x))
			++i;
	}
	return true;
}
u64 seed = 41;
u64 rnd()
{
	seed ^= seed << 13;
	seed ^= seed >> 7;
	seed ^= seed << 17;
	return seed;
}
volatile u64 sink;
int main(int argc, char**)
{
	alignas(16) std::array<v128, 1024> a, b;
	for (auto& x : a)
	{
		x._u64[0] = rnd();
		x._u64[1] = rnd();
	}
	b = a;
	for (unsigned i = 0; i < 1000000; ++i)
	{
		v128 x;
		x._u64[0] = rnd();
		x._u64[1] = rnd();
		if (old_test(x) != new_test(x))
			return 1;
	}
	for (unsigned i = 0; i < 64; ++i)
	{
		v128 x{};
		x._u32[1] = (i & 3) << 8;
		x._u32[2] = ((i >> 2) & 3) << 8;
		x._u32[3] = ((i >> 4) & 3) << 8;
		bool oracle = (i & 3) == 2 || ((i >> 2) & 3) == 2 || ((i >> 4) & 3) == 2;
		if (old_test(x) != oracle || new_test(x) != oracle)
			return 1;
	}
	for (unsigned n : {8, 32, 128, 512})
		for (unsigned mode = 0; mode < 3; ++mode)
		{
			for (auto& x : a)
			{
				x._u64[0] = rnd();
				x._u64[1] = rnd();
				if (mode == 0)
					for (unsigned k = 1; k < 4; ++k)
						x._u32[k] &= ~0x300u;
				else if (mode == 1)
					x._u32[1] = (x._u32[1] & ~0x300u) | 0x200;
			}
			b = a;
			if (hash<false>(a.data(), n) != hash<true>(a.data(), n))
				return 1;
			for (unsigned mutation = 0; mutation < n * 16; ++mutation)
			{
				reinterpret_cast<unsigned char*>(b.data())[mutation] ^= 0x81;
				if (compare<false>(a.data(), b.data(), n) != compare<true>(a.data(), b.data(), n))
					return 1;
				reinterpret_cast<unsigned char*>(b.data())[mutation] ^= 0x81;
			}
			if (argc > 1)
				continue;
			for (bool cmp : {false, true})
			{
				std::array<std::vector<double>, 2> times;
				for (unsigned round = 0; round < 12; ++round)
					for (unsigned order = 0; order < 2; ++order)
					{
						unsigned v = order ^ (round & 1);
						auto st = std::chrono::steady_clock::now();
						u64 r = 0;
						for (unsigned i = 0; i < 100000; ++i)
						{
							asm volatile("" ::: "memory");
							r += cmp ? (v ? compare<true>(a.data(), b.data(), n) : compare<false>(a.data(), b.data(), n)) : (v ? hash<true>(a.data(), n) : hash<false>(a.data(), n));
						}
						sink = r;
						double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - st).count() / 100000;
						if (round >= 2)
							times[v].push_back(ns);
					}
				double m[2];
				for (unsigned v = 0; v < 2; ++v)
				{
					std::sort(times[v].begin(), times[v].end());
					m[v] = (times[v][4] + times[v][5]) / 2;
				}
				std::printf("%s n=%u mode=%u old=%.4f new=%.4f gain=%.2f%%\n", cmp ? "compare" : "hash", n, mode, m[0], m[1], 100 * (1 - m[1] / m[0]));
				std::fflush(stdout);
			}
		}
	std::puts("PASS fragment constants: 64 type combinations, 1000000 random instructions, 32640 byte mutations");
}
