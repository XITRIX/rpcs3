#pragma once

#include <array>
#include <cstdint>

namespace vk
{
	// Valid only for one command-buffer recording and one graphics pipeline.
	// Store float bit patterns so signed zero and NaN payloads stay distinct.
	struct dynamic_state_cache
	{
		enum slot : unsigned
		{
			line_width, blend_constants, depth_bias, depth_bounds,
			stencil_write_front, stencil_write_back,
			stencil_compare_front, stencil_compare_back,
			stencil_reference_front, stencil_reference_back, count
		};

		std::array<std::array<std::uint32_t, 4>, count> values{};
		std::uint32_t valid = 0;

		void clear() { valid = 0; }
		bool update(slot index, std::array<std::uint32_t, 4> value)
		{
			const auto bit = 1u << index;
			if ((valid & bit) && values[index] == value)
			{
				return false;
			}
			values[index] = value;
			valid |= bit;
			return true;
		}
	};
}
