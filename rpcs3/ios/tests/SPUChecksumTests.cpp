#include <arm_neon.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>

extern "C" std::uint32_t spu_checksum_scalar(uint32x4_t, uint32x4_t, uint32x4_t, uint32x4_t);
extern "C" std::uint32_t spu_checksum_neon(uint32x4_t, uint32x4_t, uint32x4_t, uint32x4_t);

using checksum = std::array<std::uint32_t, 16>;
using reduction = decltype(&spu_checksum_scalar);

static std::uint32_t run(reduction function, const checksum& words)
{
	return function(vld1q_u32(words.data()), vld1q_u32(words.data() + 4),
		vld1q_u32(words.data() + 8), vld1q_u32(words.data() + 12));
}

static void check(const checksum& words)
{
	const bool expected = std::any_of(words.begin(), words.end(), [](auto word) { return word != 0; });
	if (run(spu_checksum_neon, words) != expected || run(spu_checksum_scalar, words) != expected)
	{
		std::fputs("SPU checksum reduction disagrees with scalar any-nonzero oracle\n", stderr);
		std::abort();
	}
}

static double measure(reduction function, const checksum& words)
{
	constexpr unsigned iterations = 5'000'000;
	std::uint32_t sum = 0;
	const auto start = std::chrono::steady_clock::now();
	for (unsigned i = 0; i < iterations; i++)
	{
		sum += run(function, words);
	}
	const auto end = std::chrono::steady_clock::now();
	if (sum != 0)
	{
		std::abort();
	}
	return std::chrono::duration<double, std::nano>(end - start).count() / iterations;
}

int main(int argc, char**)
{
	checksum words{};
	check(words);

	// Every bit in the full 512-bit checksum, including every byte's high bit.
	for (unsigned bit = 0; bit < 512; bit++)
	{
		words.fill(0);
		words[bit / 32] = std::uint32_t{1} << (bit % 32);
		check(words);

		// Equal differences in multiple parts must not cancel, as XOR would.
		for (unsigned part = 0; part < 4; part++)
		{
			words[part * 4 + (bit / 32) % 4] = std::uint32_t{1} << (bit % 32);
		}
		check(words);
	}

	for (const auto value : {0xffffffffu, 0x80000000u, 0x80808080u, 0x01010101u})
	{
		words.fill(value);
		check(words);
	}

	std::mt19937 random(0x535055);
	for (unsigned trial = 0; trial < 100'000; trial++)
	{
		for (auto& word : words)
		{
			word = random();
		}
		check(words);
		// Sparse mismatches test that each part reaches the final reduction.
		const auto index = trial % words.size();
		const auto value = words[index];
		words.fill(0);
		words[index] = value;
		check(words);
	}
	std::puts("SPU checksum: zero, 512 single bits, repeated-bit, high-bit, and 200000 random cases passed");

	if (argc > 1)
	{
		// Optional diagnostic benchmark; timing is never a correctness assertion.
		words.fill(0);
		std::array<double, 9> scalar{}, neon{};
		for (unsigned round = 0; round < scalar.size(); round++)
		{
			if (round % 2)
			{
				neon[round] = measure(spu_checksum_neon, words);
				scalar[round] = measure(spu_checksum_scalar, words);
			}
			else
			{
				scalar[round] = measure(spu_checksum_scalar, words);
				neon[round] = measure(spu_checksum_neon, words);
			}
		}
		std::sort(scalar.begin(), scalar.end());
		std::sort(neon.begin(), neon.end());
		std::printf("Median reduction including host call: scalar %.3f ns, NEON %.3f ns (%.2fx)\n",
			scalar[4], neon[4], scalar[4] / neon[4]);
	}
}
