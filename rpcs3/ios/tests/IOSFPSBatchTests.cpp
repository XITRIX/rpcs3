#include "ios/IOSDMACopy.h"
#include "ios/IOSTextureHash.h"
#include "util/v128.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>
#define XXH_INLINE_ALL
#include "Utilities/xxhash3.h"

namespace
{
void check(bool condition)
{
	if (!condition) std::abort();
}

struct guarded_page
{
	const std::size_t size = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
	unsigned char* base = static_cast<unsigned char*>(mmap(nullptr, size * 3,
		PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0));

	guarded_page()
	{
		check(base != MAP_FAILED);
		check(mprotect(base + size, size, PROT_READ | PROT_WRITE) == 0);
	}
	~guarded_page() { munmap(base, size * 3); }
	unsigned char* begin() const { return base + size; }
	unsigned char* end() const { return base + size * 2; }
};
}

int main()
{
	std::size_t copy_cases = 0, hash_cases = 0;
	alignas(64) std::array<unsigned char, 2048> actual{}, expected{};
	for (std::size_t length = 0; length <= 256; length += 16)
	{
		for (std::size_t src = 128; src < 1152; src += 16)
		{
			for (std::size_t dst = 128; dst < 1152; dst += 16)
			{
				for (std::size_t i = 0; i < actual.size(); ++i) actual[i] = expected[i] = static_cast<unsigned char>(i * 31 + i / 256);
				// Independent byte oracle: snapshot each individual 16-byte unit
				// before advancing, including forward-propagating overlaps.
				for (std::size_t offset = 0; offset < length; offset += 16)
				{
					std::array<unsigned char, 16> unit{};
					std::copy_n(expected.begin() + src + offset, 16, unit.begin());
					std::copy(unit.begin(), unit.end(), expected.begin() + dst + offset);
				}
				rpcs3::ios::copy_dma_vectors<v128>(actual.data() + dst, actual.data() + src, length);
				check(actual == expected);
				++copy_cases;
			}
		}
	}

	guarded_page source, destination;
	for (std::size_t n = 16; n <= 256; n += 16)
	{
		for (bool source_at_end : {false, true}) for (bool destination_at_end : {false, true})
		{
			auto* src = source_at_end ? source.end() - n : source.begin();
			auto* dst = destination_at_end ? destination.end() - n : destination.begin();
			std::fill_n(src, n, 0x75);
			rpcs3::ios::copy_dma_vectors<v128>(dst, src, n);
			check(std::equal(src, src + n, dst));
			++copy_cases;
		}
	}

	std::mt19937_64 random(178);
	std::vector<unsigned char> input(16 * 1024 * 1024 + 64);
	for (auto& byte : input) byte = static_cast<unsigned char>(random());
	alignas(64) unsigned char secret[XXH3_SECRET_DEFAULT_SIZE];
	for (std::uint64_t seed : {0ull, 1ull, 0xcbf29ce484222325ull})
	{
		XXH3_generateSecret_fromSeed(secret, seed);
		auto verify = [&](const unsigned char* ptr, std::size_t n)
		{
			check(rpcs3::ios::texture_hash_hybrid(ptr, n, secret, sizeof(secret), seed) == XXH3_64bits_withSeed(ptr, n, seed));
			++hash_cases;
		};
		for (std::size_t n = 0; n < 8192; ++n) for (std::size_t offset = 0; offset < 32; ++offset) verify(input.data() + offset, n);
		for (std::size_t n : {8192u, 16384u, 65536u, 1048576u, 16777216u}) for (std::size_t offset = 0; offset < 32; ++offset) verify(input.data() + offset, n);
		for (std::size_t n = 1; n <= source.size; ++n) verify(source.end() - n, n);
	}
	std::printf("%zu DMA overlap/bounds cases and %zu seeded hash/alignment/guard-page cases passed\n", copy_cases, hash_cases);
}
