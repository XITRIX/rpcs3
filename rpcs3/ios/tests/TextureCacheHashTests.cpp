#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#define XXH_INLINE_ALL
#include "Utilities/xxhash3.h"

int main()
{
	std::array<std::uint8_t, 65536 + 64> data{};
	std::uint64_t state = UINT64_C(0x9e3779b97f4a7c15);
	for (auto& byte : data)
	{
		state ^= state >> 12;
		state ^= state << 25;
		state ^= state >> 27;
		byte = static_cast<std::uint8_t>(state * UINT64_C(0x2545f4914f6cdd1d));
	}

	constexpr std::array<std::size_t, 12> lengths = {
		0, 1, 8, 16, 17, 128, 129, 240, 241, 4096, 16384, 65536
	};
	constexpr XXH64_hash_t seed = UINT64_C(0xcbf29ce484222325);
	constexpr std::array<std::uint8_t, 3> abc = {'a', 'b', 'c'};
	assert(XXH3_64bits_withSeed(nullptr, 0, seed) == UINT64_C(0x8854fa7f2b3a365d));
	assert(XXH3_64bits_withSeed(abc.data(), abc.size(), seed) == UINT64_C(0x297e7db54428e080));

	for (const auto length : lengths)
	{
		const auto baseline = XXH3_64bits_withSeed(data.data() + 1, length, seed);
		assert(XXH3_64bits_withSeed(data.data() + 1, length, seed) == baseline);
		assert(XXH3_64bits_withSeed(data.data() + 1, length, seed + 1) != baseline);

		if (length)
		{
			const auto changed_index = 1 + (length / 2);
			data[changed_index] ^= 0x80;
			assert(XXH3_64bits_withSeed(data.data() + 1, length, seed) != baseline);
			data[changed_index] ^= 0x80;
			assert(XXH3_64bits_withSeed(data.data() + 1, length, seed) == baseline);
		}
	}
}
