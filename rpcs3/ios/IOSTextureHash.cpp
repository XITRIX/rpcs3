#include "IOSTextureHash.h"

// Use the bundled implementation's existing six-NEON/two-scalar-lane variant.
// Keep this in its own translation unit so other hashes retain their lowering.
#define XXH_INLINE_ALL
#define XXH3_NEON_LANES 6
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#include "Utilities/xxhash3.h"
#pragma clang diagnostic pop

namespace rpcs3::ios
{
std::uint64_t texture_hash_hybrid(const void* src, std::size_t size,
	const void* secret, std::size_t secret_size, std::uint64_t seed) noexcept
{
	return XXH3_64bits_withSecretandSeed(src, size, secret, secret_size, seed);
}
}
