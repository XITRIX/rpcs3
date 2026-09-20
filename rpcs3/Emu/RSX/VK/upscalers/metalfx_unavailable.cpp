#include "stdafx.h"
#include "metalfx_spatial.h"

// Apple does not ship MetalFX in the Simulator SDK. Returning false from
// prepare preserves VKPresent's existing Bilinear fallback and saved settings.
namespace vk
{
	struct metal_fx_spatial_upscaler::implementation {};

	metal_fx_spatial_upscaler::metal_fx_spatial_upscaler()
	{
		rsx_log.warning("MetalFX Spatial is unavailable on Simulator; using Bilinear.");
	}

	metal_fx_spatial_upscaler::~metal_fx_spatial_upscaler() = default;

	bool metal_fx_spatial_upscaler::prepare(render_device&, std::uint32_t, std::uint32_t,
		std::uint32_t, std::uint32_t)
	{
		return false;
	}

	bool metal_fx_spatial_upscaler::encode() { return false; }
	viewable_image* metal_fx_spatial_upscaler::input_image() const { return nullptr; }
	viewable_image* metal_fx_spatial_upscaler::output_image() const { return nullptr; }
	void metal_fx_spatial_upscaler::reset() {}
}
