#pragma once

#include "util/video_source.h"

#include <memory>

namespace rpcs3::ios
{
class overlay_media_source final : public video_source
{
public:
	overlay_media_source();
	~overlay_media_source() override;

	overlay_media_source(const overlay_media_source&) = delete;
	overlay_media_source& operator=(const overlay_media_source&) = delete;

	void set_video_path(const std::string& video_path) override;
	void set_audio_path(const std::string& audio_path) override;
	void set_active(bool active) override;
	bool get_active() const override;
	bool has_new() const override;
	void get_image(std::vector<u8>& data, int& width, int& height, int& channels, int& bits_per_pixel) override;

private:
	class implementation;
	std::unique_ptr<implementation> m_impl;

	void notify_frame_update();
};

[[nodiscard]] std::unique_ptr<video_source> make_overlay_media_source();
}
