#include "util/v128.hpp"
#include "dma-original.h"
#include "ios/IOSDMACopy.h"
#include <array>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>
static volatile u64 sink;
__attribute__((noinline)) void old_copy(unsigned char* d, const unsigned char* s, std::size_t n)
{
	baseline::ios::copy_dma_vectors<v128>(d, s, n);
}
__attribute__((noinline)) void new_copy(unsigned char* d, const unsigned char* s, std::size_t n)
{
	rpcs3::ios::copy_dma_vectors<v128>(d, s, n);
}
int main()
{
	alignas(128) std::array<unsigned char, 32768> a{}, b{};
	for (unsigned size : {16, 32, 48, 64, 128, 144, 256, 512, 1024, 4096, 16384})
	{
		std::vector<double> times[2];
		unsigned count = 33554432 / size;
		for (unsigned rep = 0; rep < 12; ++rep)
			for (unsigned j = 0; j < 2; ++j)
			{
				unsigned v = (rep + j) % 2;
				u64 r = 0;
				auto t = std::chrono::steady_clock::now();
				for (unsigned i = 0; i < count; ++i)
				{
					asm volatile("" ::: "memory");
					if (v)
						new_copy(a.data(), b.data(), size);
					else
						old_copy(a.data(), b.data(), size);
					r += a[i % size];
				}
				sink = r;
				double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t).count() / count;
				if (rep >= 2)
					times[v].push_back(ns);
			}
		for (auto& x : times)
			std::sort(x.begin(), x.end());
		double x = (times[0][4] + times[0][5]) / 2, y = (times[1][4] + times[1][5]) / 2;
		std::printf("dma_%u baseline=%.3f candidate=%.3f ns saving=%.2f%%\n", size, x, y, 100 * (1 - y / x));
	}
}
