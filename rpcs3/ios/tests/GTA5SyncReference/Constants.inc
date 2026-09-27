	static inline void write_fragment_constants_to_buffer_sse2(const std::span<f32>& buffer, const RSXFragmentProgram& rsx_prog, const std::vector<u32>& offsets_cache, bool sanitize)
	{
		f32* dst = buffer.data();
		for (u32 offset_in_fragment_program : offsets_cache)
		{
			const char* data = static_cast<const char*>(rsx_prog.get_data()) + offset_in_fragment_program;

			const __m128i vector = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data));
			const __m128i shuffled_vector = _mm_or_si128(_mm_slli_epi16(vector, 8), _mm_srli_epi16(vector, 8));

			if (sanitize)
			{
				//Convert NaNs and Infs to 0
				const auto masked = _mm_and_si128(shuffled_vector, _mm_set1_epi32(0x7fffffff));
				const auto valid = _mm_cmplt_epi32(masked, _mm_set1_epi32(0x7f800000));
				const auto result = _mm_and_si128(shuffled_vector, valid);
				_mm_stream_si128(utils::bless<__m128i>(dst), result);
			}
			else
			{
				_mm_stream_si128(utils::bless<__m128i>(dst), shuffled_vector);
			}

			dst += 4;
		}
	}