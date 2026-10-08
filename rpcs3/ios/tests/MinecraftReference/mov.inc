extern void mov_rdata(spu_rdata_t& _dst, const spu_rdata_t& _src)
{
#if defined(ARCH_X64)
#ifndef __AVX__
	if (s_tsx_avx) [[likely]]
#endif
	{
		mov_rdata_avx(reinterpret_cast<__m256i*>(_dst), reinterpret_cast<const __m256i*>(_src));
		return;
	}

	{
		const __m128i v0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(_src + 0));
		const __m128i v1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(_src + 16));
		const __m128i v2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(_src + 32));
		const __m128i v3 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(_src + 48));
		_mm_storeu_si128(reinterpret_cast<__m128i*>(_dst + 0), v0);
		_mm_storeu_si128(reinterpret_cast<__m128i*>(_dst + 16), v1);
		_mm_storeu_si128(reinterpret_cast<__m128i*>(_dst + 32), v2);
		_mm_storeu_si128(reinterpret_cast<__m128i*>(_dst + 48), v3);
	}

	const __m128i v0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(_src + 64));
	const __m128i v1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(_src + 80));
	const __m128i v2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(_src + 96));
	const __m128i v3 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(_src + 112));
	_mm_storeu_si128(reinterpret_cast<__m128i*>(_dst + 64), v0);
	_mm_storeu_si128(reinterpret_cast<__m128i*>(_dst + 80), v1);
	_mm_storeu_si128(reinterpret_cast<__m128i*>(_dst + 96), v2);
	_mm_storeu_si128(reinterpret_cast<__m128i*>(_dst + 112), v3);
#else
	std::memcpy(_dst, _src, 128);
#endif
}
