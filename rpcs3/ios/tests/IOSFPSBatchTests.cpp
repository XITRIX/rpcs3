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

	// Every aligned MFC length, including the 64-byte unroll boundary and
	// all tails, against an independent per-vector snapshot oracle.
	alignas(64) std::array<unsigned char, 17408> large_actual{}, large_expected{};
	for (std::size_t length = 0; length <= 16384; length += 16)
	{
		for (int delta = -256; delta <= 256; delta += 16)
		{
			for (std::size_t i = 0; i < large_actual.size(); ++i)
				large_actual[i] = large_expected[i] = static_cast<unsigned char>(i * 37 + i / 128);
			const std::size_t src = 512, dst = static_cast<std::size_t>(512 + delta);
			for (std::size_t offset = 0; offset < length; offset += 16)
			{
				std::array<unsigned char, 16> unit{};
				std::copy_n(large_expected.begin() + src + offset, 16, unit.begin());
				std::copy(unit.begin(), unit.end(), large_expected.begin() + dst + offset);
			}
			rpcs3::ios::copy_dma_vectors<v128>(large_actual.data() + dst, large_actual.data() + src, length);
			check(large_actual == large_expected);
			++copy_cases;
		}
	}

	guarded_page source, destination;
	for (std::size_t n = 16; n <= std::min<std::size_t>(source.size, 16384); n += 16)
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

	// Different virtual addresses can still alias the same guest storage.
	// A non-overlap test on pointers must never permit bulk load/store copies.
	char alias_path[] = "/tmp/rpcs3-dma-alias-XXXXXX";
	const int alias_fd = mkstemp(alias_path);
	check(alias_fd >= 0);
	unlink(alias_path);
	constexpr std::size_t alias_size = 32768;
	check(ftruncate(alias_fd, alias_size) == 0);
	auto* alias_a = static_cast<unsigned char*>(mmap(nullptr, alias_size, PROT_READ | PROT_WRITE, MAP_SHARED, alias_fd, 0));
	auto* alias_b = static_cast<unsigned char*>(mmap(nullptr, alias_size, PROT_READ | PROT_WRITE, MAP_SHARED, alias_fd, 0));
	check(alias_a != MAP_FAILED && alias_b != MAP_FAILED);
	std::array<unsigned char, alias_size> alias_expected{};
	for (std::size_t length : {144u, 256u, 512u, 1024u, 8192u, 16384u})
	{
		for (int delta = -256; delta <= 256; delta += 16)
		{
			for (std::size_t i = 0; i < alias_size; ++i)
				alias_a[i] = alias_expected[i] = static_cast<unsigned char>(i * 37 + i / 128);
			const std::size_t src = 512, dst = static_cast<std::size_t>(512 + delta);
			for (std::size_t offset = 0; offset < length; offset += 16)
			{
				std::array<unsigned char, 16> unit{};
				std::copy_n(alias_expected.begin() + src + offset, 16, unit.begin());
				std::copy(unit.begin(), unit.end(), alias_expected.begin() + dst + offset);
			}
			rpcs3::ios::copy_dma_vectors<v128>(alias_b + dst, alias_a + src, length);
			check(std::equal(alias_expected.begin(), alias_expected.end(), alias_a));
			++copy_cases;
		}
	}
	munmap(alias_a, alias_size);
	munmap(alias_b, alias_size);
	close(alias_fd);

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

	// The fixed 192-byte secret fast path must retain withSecretandSeed's
	// contract for arbitrary secrets, including unaligned and nonstandard sizes.
	// Long inputs use the supplied secret; short inputs use the seed instead.
	std::array<unsigned char, 256 + 16> custom_secret;
	for (auto& byte : custom_secret) byte = static_cast<unsigned char>(random());
	for (std::size_t secret_size : {136u, 191u, 192u, 193u, 256u})
	{
		for (std::size_t offset = 0; offset < 16; ++offset)
		{
			for (std::uint64_t seed : {0ull, 0xcbf29ce484222325ull})
			{
				for (std::size_t n : {0u, 1u, 16u, 128u, 240u, 241u, 1023u, 1024u, 1025u,
					4095u, 4096u, 4097u, 4159u, 4160u, 4161u, 5119u, 5120u, 5121u,
					8191u, 8192u, 8193u, 16383u, 16384u, 16385u})
				{
					const auto* ptr = input.data() + offset;
					const auto* key = custom_secret.data() + offset;
					check(rpcs3::ios::texture_hash_hybrid(ptr, n, key, secret_size, seed) ==
						XXH3_64bits_withSecretandSeed(ptr, n, key, secret_size, seed));
					++hash_cases;
				}
			}
		}
	}

	// Bound both the source and the secret by inaccessible pages. Exercise the
	// specialized path and its generic fallback, including each stripe tail.
	guarded_page secret_page;
	for (std::size_t secret_size : {136u, 191u, 192u, 193u, 256u})
	{
		for (bool key_at_end : {false, true})
		{
			auto* key = key_at_end ? secret_page.end() - secret_size : secret_page.begin();
			std::copy_n(custom_secret.data(), secret_size, key);
			for (std::size_t n = 4096; n <= source.size; n += 17)
			{
				for (bool data_at_end : {false, true})
				{
					const auto* ptr = data_at_end ? source.end() - n : source.begin();
					check(rpcs3::ios::texture_hash_hybrid(ptr, n, key, secret_size, 1) ==
						XXH3_64bits_withSecretandSeed(ptr, n, key, secret_size, 1));
					++hash_cases;
				}
			}
		}
	}
	std::printf("%zu DMA overlap/bounds cases and %zu seeded hash/alignment/guard-page cases passed\n", copy_cases, hash_cases);
}
