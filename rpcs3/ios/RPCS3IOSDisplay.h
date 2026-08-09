#pragma once

#include "RPCS3IOS.h"

#include <cmath>
#include <mutex>

namespace rpcs3::ios
{
struct display_surface_snapshot
{
	void* metal_layer = nullptr;
	uint32_t width = 0;
	uint32_t height = 0;
	float refresh_rate = 0;

	bool valid() const noexcept
	{
		return metal_layer && width && height && std::isfinite(refresh_rate) && refresh_rate > 0;
	}
};

inline rpcs3_ios_status validate_display_surface_contract(
	const rpcs3_ios_display_surface* surface) noexcept
{
	if (!surface)
	{
		return RPCS3_IOS_OK;
	}

	return surface->struct_size >= sizeof(rpcs3_ios_display_surface) &&
		surface->metal_layer && surface->width && surface->height &&
		std::isfinite(surface->refresh_rate) && surface->refresh_rate > 0
		? RPCS3_IOS_OK
		: RPCS3_IOS_INVALID_ARGUMENT;
}

class display_surface_registry final
{
public:
	rpcs3_ios_status update(
		const rpcs3_ios_display_surface* surface,
		bool emulation_stopped) noexcept
	{
		if (const auto result = validate_display_surface_contract(surface);
			result != RPCS3_IOS_OK)
		{
			return result;
		}

		std::lock_guard lock(m_mutex);
		if (!surface)
		{
			if (m_surface.metal_layer && !emulation_stopped)
			{
				return RPCS3_IOS_INVALID_STATE;
			}
			m_surface = {};
			return RPCS3_IOS_OK;
		}

		if (m_surface.metal_layer &&
			m_surface.metal_layer != surface->metal_layer &&
			!emulation_stopped)
		{
			return RPCS3_IOS_INVALID_STATE;
		}

		m_surface = {
			surface->metal_layer,
			surface->width,
			surface->height,
			surface->refresh_rate,
		};
		return RPCS3_IOS_OK;
	}

	display_surface_snapshot snapshot() const noexcept
	{
		std::lock_guard lock(m_mutex);
		return m_surface;
	}

	void clear() noexcept
	{
		std::lock_guard lock(m_mutex);
		m_surface = {};
	}

private:
	mutable std::mutex m_mutex;
	display_surface_snapshot m_surface;
};
}
