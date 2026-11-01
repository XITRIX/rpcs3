#pragma once

#include <bit>
#include <cstdint>
#include <cstring>

namespace rpcs3::ios::aot
{
	// Callers validate the guest mapping/local-store bounds first. Reducing the
	// complete mismatch permits LLVM to vectorize byte reversal and comparison;
	// no byte is skipped, including the last word of a negative dispatch cache.
	inline bool matches_guest_words(const void* guest, const std::uint32_t* words, std::uint32_t count, bool sparse_spu = false) noexcept
	{
		if (!count || count > 65536)
			return false;
		const auto* bytes = static_cast<const unsigned char*>(guest);
		const auto read_word = [&](std::uint32_t i)
		{
			std::uint32_t word;
			std::memcpy(&word, bytes + i * 4, sizeof(word));
			if constexpr (std::endian::native == std::endian::little)
				word = __builtin_bswap32(word);
			return word;
		};
		if (count == 1)
			return (sparse_spu && !words[0]) || read_word(0) == words[0];
		std::uint32_t mismatch = 0;
		for (std::uint32_t i = 0; i < count; ++i)
			mismatch |= (read_word(i) ^ words[i]) & (0u - std::uint32_t(!sparse_spu || words[i] != 0));
		return mismatch == 0;
	}
} // namespace rpcs3::ios::aot
