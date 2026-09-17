#include <arm_neon.h>
#include "Emu/CPU/sse2neon.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

int main()
{
	const std::array<std::int32_t, 12> values{
		0, 1, -1, INT32_MIN, INT32_MAX, 0x0ffb4880,
		static_cast<std::int32_t>(0xde189a48u), 0x20cd02f0, 0x72db5930,
		-17, 0x40000000, -0x40000000};
	for (unsigned immediate = 0; immediate < 128; ++immediate)
	{
		for (unsigned base = 0; base < values.size(); base += 4)
		{
			const auto input = vld1q_s32(values.data() + base);
			// ROTMAI passes this expression directly to the SSE intrinsic.
			const auto result = _mm_srai_epi32(input, (0 - immediate) & 0x3f);
			std::array<std::int32_t, 4> actual{};
			vst1q_s32(actual.data(), result);
			for (unsigned lane = 0; lane < 4; ++lane)
			{
				// A signed 64-bit shift also covers counts >= 32 without UB.
				const auto expected = static_cast<std::int32_t>(
					static_cast<std::int64_t>(values[base + lane]) >> ((0 - immediate) & 63));
				if (actual[lane] != expected)
				{
					std::fprintf(stderr, "ROTMAI immediate=%u input=%d: expected %d, got %d\n",
						immediate, values[base + lane], expected, actual[lane]);
					return EXIT_FAILURE;
				}
			}
		}
	}
}
