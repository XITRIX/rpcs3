#pragma once

#include "VKFormats.h"
#include "../Utils/color_utils.hpp"

namespace vk
{
	// The Vulkan texture record extends the shared 48-byte RSX record by one vec4.
	inline constexpr u32 fragment_texture_record_size = 64;
	inline constexpr u32 color_border_s = 1u << 29;
	inline constexpr u32 color_border_t = 1u << 30;
	inline constexpr u32 color_border_linear = 1u << 31;

	struct color_border_state
	{
		u32 control = 0;
		color4f color{0.f};
	};

	// A single level with matching min/mag filters has an unambiguous footprint.
	// Keep native custom borders and unsupported footprints on their existing path.
	inline color_border_state get_color_border_state(
		const auto& device, const auto& tex, const auto& descriptor,
		VkFilter min_filter, VkFilter mag_filter)
	{
		if (device.get_custom_border_color_support() ||
			descriptor.format_class != RSX_FORMAT_CLASS_COLOR || descriptor.samples != 1 ||
			descriptor.image_type != rsx::texture_dimension_extended::texture_dimension_2d ||
			tex.get_exact_mipmap_count() != 1 || descriptor.texcoord_xform.clamp ||
			tex.argb_signed() || descriptor.format_ex.host_features ||
			tex.max_aniso() != rsx::texture_max_anisotropy::x1 || min_filter != mag_filter ||
			get_border_color(tex.border_color(false)) != VK_BORDER_COLOR_FLOAT_CUSTOM_EXT)
		{
			return {};
		}

		const u32 axes = (tex.wrap_s() == rsx::texture_wrap_mode::border ? color_border_s : 0u) |
			(tex.wrap_t() == rsx::texture_wrap_mode::border ? color_border_t : 0u);
		if (!axes)
		{
			return {};
		}

		return {axes | (min_filter == VK_FILTER_LINEAR ? color_border_linear : 0u), tex.remapped_border_color(false)};
	}
}
