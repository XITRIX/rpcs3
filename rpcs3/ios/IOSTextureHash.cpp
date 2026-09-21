#include "IOSTextureHash.h"

// Use the bundled implementation's existing six-NEON/two-scalar-lane variant.
// Keep this in its own translation unit so other hashes retain their lowering.
#define XXH_INLINE_ALL
#define XXH3_NEON_LANES 6
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#include "Utilities/xxhash3.h"
#pragma clang diagnostic pop

namespace
{
#if defined(__aarch64__) || defined(__arm64__)
// The texture cache reuses a 192-byte secret. Four stripes per iteration give
// Clang room to overlap independent NEON/scalar work without changing XXH3's
// accumulator order, block scrambles, final stripe or bytes read.
XXH_FORCE_INLINE void accumulate_texture_stripes(xxh_u64* XXH_RESTRICT acc,
	const xxh_u8* XXH_RESTRICT input, const xxh_u8* XXH_RESTRICT secret,
	std::size_t stripe_count)
{
#if defined(__clang__)
#pragma clang loop unroll_count(4)
#endif
	for (std::size_t stripe = 0; stripe < stripe_count; ++stripe)
	{
		const auto* data = input + stripe * XXH_STRIPE_LEN;
		XXH_PREFETCH(data + XXH_PREFETCH_DIST);
		XXH3_accumulate_512_neon(acc, data, secret + stripe * XXH_SECRET_CONSUME_RATE);
	}
}
#endif
}

namespace rpcs3::ios
{
std::uint64_t texture_hash_hybrid(const void* src, std::size_t size,
	const void* secret, std::size_t secret_size, std::uint64_t seed) noexcept
{
#if defined(__aarch64__) || defined(__arm64__)
	if (size >= 4096 && secret_size == XXH3_SECRET_DEFAULT_SIZE)
	{
		// Constant block geometry also removes the generic secret-size division.
		// For long inputs withSecretandSeed uses the supplied secret, not seed.
		return XXH3_hashLong_64b_internal(src, size, secret, XXH3_SECRET_DEFAULT_SIZE,
			accumulate_texture_stripes, XXH3_scrambleAcc_neon);
	}
#endif
	return XXH3_64bits_withSecretandSeed(src, size, secret, secret_size, seed);
}
}
