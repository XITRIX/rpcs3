#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

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
	template <typename Sampler, typename Context, std::size_t Count>
	bool invalidate_sampler_context(const std::array<Sampler, Count>& samplers,
		std::uint32_t& dirty, Context context)
	{
		static_assert(Count <= 32);
		bool invalidated = false;
		for (std::size_t i = 0; i < Count; ++i)
		{
			if (samplers[i] && samplers[i]->upload_context == context)
			{
				dirty |= std::uint32_t{1} << i;
				invalidated = true;
			}
		}
		return invalidated;
	}
}
