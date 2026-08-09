#include "stdafx.h"
#include "IOSGSFrame.h"

#include <algorithm>
#include <limits>

namespace rpcs3::ios
{
gs_frame::gs_frame(display_surface_registry& surface)
	: m_surface_registry(surface)
	, m_initial_surface(surface.snapshot())
{
	ensure(m_initial_surface.valid());
}

void gs_frame::close()
{
	m_shown = false;
}

void gs_frame::reset()
{
}

bool gs_frame::shown()
{
	return m_shown;
}

void gs_frame::hide()
{
	m_shown = false;
}

void gs_frame::show()
{
	m_shown = true;
}

void gs_frame::toggle_fullscreen()
{
}

void gs_frame::delete_context(draw_context_t)
{
}

draw_context_t gs_frame::make_context()
{
	return nullptr;
}

void gs_frame::set_current(draw_context_t)
{
}

void gs_frame::flip(draw_context_t, bool)
{
}

display_surface_snapshot gs_frame::current_surface() const noexcept
{
	const display_surface_snapshot current = m_surface_registry.snapshot();
	return current.metal_layer == m_initial_surface.metal_layer && current.valid()
		? current
		: m_initial_surface;
}

int gs_frame::client_width()
{
	return static_cast<int>(std::min<uint32_t>(
		current_surface().width, std::numeric_limits<int>::max()));
}

int gs_frame::client_height()
{
	return static_cast<int>(std::min<uint32_t>(
		current_surface().height, std::numeric_limits<int>::max()));
}

f64 gs_frame::client_display_rate()
{
	return std::max<f64>(20., current_surface().refresh_rate);
}

bool gs_frame::has_alpha()
{
	return false;
}

display_handle_t gs_frame::handle() const
{
	return m_initial_surface.metal_layer;
}

bool gs_frame::can_consume_frame() const
{
	return false;
}

void gs_frame::present_frame(std::vector<u8>&&, u32, u32, u32, bool) const
{
}

void gs_frame::take_screenshot(std::vector<u8>&&, u32, u32, bool)
{
}

void gs_frame::update_title(double)
{
}
}
