#pragma once

#include <arm_neon.h>
#include <cstdint>

namespace aarch64
{
// The native reservation fingerprint is a modulo-2^32 sum of all 32 words.
// Keep the horizontal reduction in NEON until the single scalar result.
inline std::uint32_t spu_rdata_hash32(const void* source)
{
	const auto* bytes = static_cast<const std::uint8_t*>(source);
	auto load = [&](unsigned offset) { return vreinterpretq_u32_u8(vld1q_u8(bytes + offset)); };
	const auto a = vaddq_u32(load(0), load(16));
	const auto b = vaddq_u32(load(32), load(48));
	const auto c = vaddq_u32(load(64), load(80));
	const auto d = vaddq_u32(load(96), load(112));
	return vaddvq_u32(vaddq_u32(vaddq_u32(a, b), vaddq_u32(c, d)));
}
}
