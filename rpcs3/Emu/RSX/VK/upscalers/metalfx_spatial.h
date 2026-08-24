#pragma once

#include <cstdint>
#include <memory>

namespace vk
{
	class render_device;
	class viewable_image;

	class metal_fx_spatial_upscaler final
	{
		struct implementation;
		std::unique_ptr<implementation> m_impl;

	public:
		metal_fx_spatial_upscaler();
		~metal_fx_spatial_upscaler();

		metal_fx_spatial_upscaler(const metal_fx_spatial_upscaler&) = delete;
		metal_fx_spatial_upscaler& operator=(const metal_fx_spatial_upscaler&) = delete;

		bool prepare(
			render_device& device,
			std::uint32_t input_width,
			std::uint32_t input_height,
			std::uint32_t output_width,
			std::uint32_t output_height);

		bool encode();
		viewable_image* input_image() const;
		viewable_image* output_image() const;
		void reset();
	};
}
