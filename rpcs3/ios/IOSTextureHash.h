#pragma once

#include <cstddef>
#include <cstdint>

namespace rpcs3::ios
{
// Same seeded XXH3 result as the regular implementation. Callers select this
// only for large inputs; short hashes retain the existing eight-NEON-lane path.
std::uint64_t texture_hash_hybrid(const void* src, std::size_t size,
	const void* secret, std::size_t secret_size, std::uint64_t seed) noexcept;
}
