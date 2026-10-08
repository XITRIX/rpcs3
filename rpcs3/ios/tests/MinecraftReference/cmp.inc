static FORCE_INLINE int16x8_t cmp16_pair_accum_arm64(
	int16x8_t acc, const v128& lhs0, const v128& rhs0, const v128& lhs1, const v128& rhs1)
{
	const int16x8_t eq0 = vreinterpretq_s16_u16(vceqq_u16(static_cast<uint16x8_t>(lhs0), static_cast<uint16x8_t>(rhs0)));
	const int16x8_t eq1 = vreinterpretq_s16_u16(vceqq_u16(static_cast<uint16x8_t>(lhs1), static_cast<uint16x8_t>(rhs1)));
	return vmlaq_s16(acc, eq0, eq1);
}
extern bool cmp_rdata(const spu_rdata_t& _lhs, const spu_rdata_t& _rhs)
{
#if defined(ARCH_X64)
#ifndef __AVX__
	if (s_tsx_avx) [[likely]]
#endif
	{
		return cmp_rdata_avx(reinterpret_cast<const __m256i*>(_lhs), reinterpret_cast<const __m256i*>(_rhs));
	}
#endif

	const auto lhs = reinterpret_cast<const v128*>(_lhs);
	const auto rhs = reinterpret_cast<const v128*>(_rhs);
#if defined(ARCH_ARM64)
	int16x8_t hits = vdupq_n_s16(0);
	hits = cmp16_pair_accum_arm64(hits, lhs[0], rhs[0], lhs[1], rhs[1]);
	hits = cmp16_pair_accum_arm64(hits, lhs[2], rhs[2], lhs[3], rhs[3]);
	hits = cmp16_pair_accum_arm64(hits, lhs[4], rhs[4], lhs[5], rhs[5]);
	hits = cmp16_pair_accum_arm64(hits, lhs[6], rhs[6], lhs[7], rhs[7]);

	return vaddvq_s16(hits) == 32;
#else
	const v128 a = (lhs[0] ^ rhs[0]) | (lhs[1] ^ rhs[1]);
	const v128 c = (lhs[4] ^ rhs[4]) | (lhs[5] ^ rhs[5]);
	const v128 b = (lhs[2] ^ rhs[2]) | (lhs[3] ^ rhs[3]);
	const v128 d = (lhs[6] ^ rhs[6]) | (lhs[7] ^ rhs[7]);
	const v128 r = (a | b) | (c | d);
	return gv_testz(r);
#endif
}
