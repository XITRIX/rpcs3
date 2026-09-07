#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <span>

namespace rsx
{
	// CPU readback can cover less memory than the Morton-addressed texture extent.
	// Keep the original swizzle's addressing, but never access bytes outside either
	// span. The caller snapshots the input first; untouched output bytes stay resident.
	template <typename T>
	void convert_linear_swizzle_bounded(std::span<const std::uint8_t> input, std::span<std::uint8_t> output,
		std::uint16_t width, std::uint16_t height, std::uint32_t pitch)
	{
		static_assert(sizeof(T) == 2 || sizeof(T) == 4);
		if (!width || !height || output.empty())
		{
			return;
		}

		const auto log2_width = std::bit_width(std::uint32_t{width} - 1);
		const auto log2_height = std::bit_width(std::uint32_t{height} - 1);
		// Wide arithmetic also covers the largest u16 dimensions and u32 row pitch.
		const std::uint64_t limit = std::uint64_t{1} << (2 * std::min(log2_width, log2_height));
		const std::uint64_t x_mask = 0x55555555ull | ~(limit - 1);
		const std::uint64_t y_mask = 0xaaaaaaaaull & (limit - 1);
		const std::uint64_t pitch_in_blocks = pitch / sizeof(T);
		std::uint64_t offset_y = 0;
		std::uint64_t offset_x0 = 0;

		for (std::uint32_t y = 0; y < height; ++y)
		{
			auto offset_x = offset_x0;
			for (std::uint32_t x = 0; x < width; ++x)
			{
				const auto dst = (offset_y + offset_x) * sizeof(T);
				if (dst < output.size())
				{
					const auto src = (y * pitch_in_blocks + x) * sizeof(T);
					const auto count = std::min<std::uint64_t>(sizeof(T), output.size() - dst);
					std::uint8_t texel[sizeof(T)]{};
					if (src < input.size())
					{
						std::memcpy(texel, input.data() + src, std::min<std::uint64_t>(sizeof(T), input.size() - src));
					}
					std::memcpy(output.data() + dst, texel, count);
				}
				offset_x = (offset_x - x_mask) & x_mask;
			}
			offset_y = (offset_y - y_mask) & y_mask;
			if (!offset_y)
			{
				offset_x0 += limit;
			}
		}
	}
}
