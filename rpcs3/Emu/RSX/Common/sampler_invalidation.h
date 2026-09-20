#pragma once

#include <array>
#include <cstddef>

namespace rsx
{
	template <typename Sampler, typename Context, std::size_t Count>
	bool invalidate_sampler_context(const std::array<Sampler, Count>& samplers,
		std::array<bool, Count>& dirty, Context context)
	{
		bool invalidated = false;
		for (std::size_t i = 0; i < Count; ++i)
		{
			if (samplers[i] && samplers[i]->upload_context == context)
			{
				// A global dirty flag is consumed by the next shader's referenced
				// slots only. Keep invalidation pending for every cached descriptor,
				// including slots that are currently disabled or unreferenced.
				dirty[i] = true;
				invalidated = true;
			}
		}
		return invalidated;
	}
}
