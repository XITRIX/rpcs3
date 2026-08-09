#pragma once

#include "RPCS3IOSDisplay.h"
#include "Emu/RSX/GSFrameBase.h"

#include <atomic>

namespace rpcs3::ios
{
class gs_frame final : public GSFrameBase
{
public:
	explicit gs_frame(display_surface_registry& surface);

	void close() override;
	void reset() override;
	bool shown() override;
	void hide() override;
	void show() override;
	void toggle_fullscreen() override;

	void delete_context(draw_context_t context) override;
	draw_context_t make_context() override;
	void set_current(draw_context_t context) override;
	void flip(draw_context_t context, bool skip_frame = false) override;
	int client_width() override;
	int client_height() override;
	f64 client_display_rate() override;
	bool has_alpha() override;

	display_handle_t handle() const override;

	bool can_consume_frame() const override;
	void present_frame(std::vector<u8>&& data, u32 pitch, u32 width, u32 height, bool is_bgra) const override;
	void take_screenshot(std::vector<u8>&& data, u32 width, u32 height, bool is_bgra) override;
	void update_title(double fps = 0.0) override;

private:
	display_surface_snapshot current_surface() const noexcept;

	display_surface_registry& m_surface_registry;
	display_surface_snapshot m_initial_surface;
	std::atomic_bool m_shown{true};
};
}
