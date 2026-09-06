#pragma once
#include <arm_neon.h>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace aarch64
{
// Return the sole changed 16-byte block in a 128-byte reservation, or size_t(-1).
// All bytes are read; this does not alter the surrounding reservation protocol.
inline std::size_t spu_scan16_rdata(const void* lhs, const void* rhs)
{
	const auto* a = static_cast<const std::uint8_t*>(lhs);
	const auto* b = static_cast<const std::uint8_t*>(rhs);
	const auto d0 = veorq_u8(vld1q_u8(a), vld1q_u8(b));
	const auto d1 = veorq_u8(vld1q_u8(a + 16), vld1q_u8(b + 16));
	const auto d2 = veorq_u8(vld1q_u8(a + 32), vld1q_u8(b + 32));
	const auto d3 = veorq_u8(vld1q_u8(a + 48), vld1q_u8(b + 48));
	const auto d4 = veorq_u8(vld1q_u8(a + 64), vld1q_u8(b + 64));
	const auto d5 = veorq_u8(vld1q_u8(a + 80), vld1q_u8(b + 80));
	const auto d6 = veorq_u8(vld1q_u8(a + 96), vld1q_u8(b + 96));
	const auto d7 = veorq_u8(vld1q_u8(a + 112), vld1q_u8(b + 112));
	const auto p03 = vpmaxq_u8(vpmaxq_u8(d0, d1), vpmaxq_u8(d2, d3));
	const auto p47 = vpmaxq_u8(vpmaxq_u8(d4, d5), vpmaxq_u8(d6, d7));
	const auto pairs = vpmaxq_u8(p03, p47);
	// The low eight bytes now hold one unsigned maximum per 16-byte block.
	const auto blocks = vget_low_u8(vpmaxq_u8(pairs, pairs));
	const uint8x8_t weights = {1, 2, 4, 8, 16, 32, 64, 128};
	const std::uint32_t mask = vaddv_u8(vand_u8(vcgt_u8(blocks, vdup_n_u8(0)), weights));
	return std::has_single_bit(mask) ? std::countr_zero(mask) : std::numeric_limits<std::size_t>::max();
}
}
