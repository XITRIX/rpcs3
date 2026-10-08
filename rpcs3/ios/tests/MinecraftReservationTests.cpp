#include "ios/IOSReservationData.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>
using spu_rdata_t = std::byte[128];
extern bool cmp_rdata(const spu_rdata_t&, const spu_rdata_t&);
extern void mov_rdata(spu_rdata_t&, const spu_rdata_t&);
static volatile u64 sink;
template <class F>
double measure(F f, unsigned n)
{
	u64 r = 0;
	auto t = std::chrono::steady_clock::now();
	for (unsigned i = 0; i < n; ++i)
	{
		asm volatile("" ::: "memory");
		r += f(i);
	}
	sink = r;
	return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t).count() / n;
}
template <class A, class B>
void bench(const char* name, A a, B b, unsigned n = 1000000)
{
	std::vector<double> t[2];
	for (unsigned rep = 0; rep < 12; ++rep)
		for (unsigned j = 0; j < 2; ++j)
		{
			unsigned v = (rep + j) % 2;
			double x = v ? measure(b, n) : measure(a, n);
			if (rep >= 2)
				t[v].push_back(x);
		}
	for (auto& v : t)
		std::sort(v.begin(), v.end());
	double x = (t[0][4] + t[0][5]) / 2, y = (t[1][4] + t[1][5]) / 2;
	std::printf("%s baseline=%.3f candidate=%.3f ns saving=%.2f%%\n", name, x, y, 100 * (1 - y / x));
}
int main(int argc, char**)
{
	std::mt19937 rng(310);
	alignas(128) std::array<std::byte, 4096> a, b, c;
	auto check = [&](unsigned off)
	{
		auto& x = *reinterpret_cast<spu_rdata_t*>(a.data() + off);
		auto& y = *reinterpret_cast<spu_rdata_t*>(b.data() + off);
		assert(cmp_rdata(x, y) == rpcs3::ios::compare_reservation_data(x, y));
		assert(cmp_rdata(x, y) == (std::memcmp(x, y, 128) == 0));
	};
	for (unsigned off = 0; off <= 128; off += 16)
	{
		for (auto& v : a)
			v = std::byte(rng());
		b = a;
		check(off);
		for (unsigned byte = 0; byte < 128; ++byte)
			for (unsigned bit = 0; bit < 8; ++bit)
			{
				b = a;
				b[off + byte] ^= std::byte(1u << bit);
				check(off);
			}
	}
	for (unsigned n = 0; n < 50000; ++n)
	{
		for (auto& v : a)
			v = std::byte(rng());
		b.fill(std::byte(0xa5));
		c = b;
		unsigned source = (rng() % 240) * 16, dst = (rng() % 240) * 16;
		mov_rdata(*reinterpret_cast<spu_rdata_t*>(b.data() + dst), *reinterpret_cast<spu_rdata_t*>(a.data() + source));
		rpcs3::ios::copy_reservation_data(*reinterpret_cast<spu_rdata_t*>(c.data() + dst), *reinterpret_cast<spu_rdata_t*>(a.data() + source));
		assert(b == c);
		assert(std::memcmp(b.data() + dst, a.data() + source, 128) == 0);
		b = a;
		if (n % 2)
			b[(rng() % 256) * 16] ^= std::byte(1);
		check(0);
	}
	auto page = sysconf(_SC_PAGESIZE);
	auto* mem = static_cast<std::byte*>(mmap(nullptr, page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0));
	assert(mem != MAP_FAILED && mprotect(mem + page, page, PROT_READ | PROT_WRITE) == 0);
	for (unsigned off : {0u, unsigned(page) - 128})
	{
		auto& x = *reinterpret_cast<spu_rdata_t*>(mem + page + off);
		auto& y = *reinterpret_cast<spu_rdata_t*>(b.data());
		mov_rdata(x, y);
		assert(cmp_rdata(x, y));
		rpcs3::ios::copy_reservation_data(x, y);
		assert(rpcs3::ios::compare_reservation_data(x, y));
	}
	munmap(mem, page * 3);
	std::puts("PASS: separate-TU originals, exact 128 bytes, every bit, randomized destinations, bounded guard pages");
	if (argc == 1)
		return 0;
	for (auto& v : a)
		v = std::byte(rng());
	b = a;
	bench("FIFO_compare_equal", [&](unsigned i)
		{
			auto off = (i % 240) * 16;
			return cmp_rdata(*reinterpret_cast<spu_rdata_t*>(a.data() + off), *reinterpret_cast<spu_rdata_t*>(b.data() + off));
		},
		[&](unsigned i)
		{
			auto off = (i % 240) * 16;
			return rpcs3::ios::compare_reservation_data(*reinterpret_cast<spu_rdata_t*>(a.data() + off), *reinterpret_cast<spu_rdata_t*>(b.data() + off));
		});
	b[64] ^= std::byte(1);
	bench("FIFO_compare_mixed", [&](unsigned i)
		{
			auto off = (i % 240) * 16;
			return cmp_rdata(*reinterpret_cast<spu_rdata_t*>(a.data() + off), *reinterpret_cast<spu_rdata_t*>(b.data() + off));
		},
		[&](unsigned i)
		{
			auto off = (i % 240) * 16;
			return rpcs3::ios::compare_reservation_data(*reinterpret_cast<spu_rdata_t*>(a.data() + off), *reinterpret_cast<spu_rdata_t*>(b.data() + off));
		});
	bench("FIFO_copy", [&](unsigned i)
		{
			auto off = (i % 240) * 16;
			mov_rdata(*reinterpret_cast<spu_rdata_t*>(b.data() + off), *reinterpret_cast<spu_rdata_t*>(a.data() + off));
			return u8(b[off + i % 128]);
		},
		[&](unsigned i)
		{
			auto off = (i % 240) * 16;
			rpcs3::ios::copy_reservation_data(*reinterpret_cast<spu_rdata_t*>(b.data() + off), *reinterpret_cast<spu_rdata_t*>(a.data() + off));
			return u8(b[off + i % 128]);
		});
	bench("FIFO_copy_compare", [&](unsigned i)
		{
			auto off = (i % 240) * 16;
			auto& x = *reinterpret_cast<spu_rdata_t*>(a.data() + off);
			auto& y = *reinterpret_cast<spu_rdata_t*>(b.data() + off);
			mov_rdata(y, x);
			asm volatile("" ::: "memory");
			return cmp_rdata(y, x);
		},
		[&](unsigned i)
		{
			auto off = (i % 240) * 16;
			auto& x = *reinterpret_cast<spu_rdata_t*>(a.data() + off);
			auto& y = *reinterpret_cast<spu_rdata_t*>(b.data() + off);
			rpcs3::ios::copy_reservation_data(y, x);
			asm volatile("" ::: "memory");
			return rpcs3::ios::compare_reservation_data(y, x);
		});
}
