#include "stdafx.h"
#include "RPCS3IOSOverlayMedia.h"
#include "RPCS3IOSOverlayMediaBuffer.h"

#include "Emu/Audio/IOS/IOSAudioBackend.h"
#include "Emu/Audio/audio_utils.h"
#include "util/media_utils.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>

#ifdef _MSC_VER
#pragma warning(push, 0)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif
extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libswresample/swresample.h"
#include "libswscale/swscale.h"
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#endif

LOG_CHANNEL(IOSOverlayMedia, "iOS Overlay Media");

namespace rpcs3::ios
{
namespace
{
constexpr u32 output_sample_rate = 48'000;
constexpr u32 output_channels = 2;
constexpr std::size_t pcm_capacity_samples = output_sample_rate * output_channels * 4;

struct ffmpeg_decoder
{
	AVFormatContext* format = nullptr;
	AVCodecContext* codec = nullptr;
	AVPacket* packet = nullptr;
	AVFrame* frame = nullptr;
	AVStream* stream = nullptr;
	int stream_index = -1;

	~ffmpeg_decoder()
	{
		if (frame)
		{
			av_frame_free(&frame);
		}
		if (packet)
		{
			av_packet_free(&packet);
		}
		if (codec)
		{
			avcodec_free_context(&codec);
		}
		if (format)
		{
			avformat_close_input(&format);
		}
	}

	bool open(const std::string& path, AVMediaType media_type)
	{
		int error = avformat_open_input(&format, path.c_str(), nullptr, nullptr);
		if (error < 0)
		{
			IOSOverlayMedia.error("Could not open '%s': %s", path, utils::av_error_to_string(error));
			return false;
		}

		error = avformat_find_stream_info(format, nullptr);
		if (error < 0)
		{
			IOSOverlayMedia.error("Could not inspect '%s': %s", path, utils::av_error_to_string(error));
			return false;
		}

		const AVCodec* decoder = nullptr;
		stream_index = av_find_best_stream(format, media_type, -1, -1, &decoder, 0);
		if (stream_index < 0 || !decoder)
		{
			IOSOverlayMedia.error("Could not find a supported %s stream in '%s'", media_type == AVMEDIA_TYPE_AUDIO ? "audio" : "video", path);
			return false;
		}

		stream = format->streams[stream_index];
		codec = avcodec_alloc_context3(decoder);
		if (!codec)
		{
			IOSOverlayMedia.error("Could not allocate the decoder for '%s'", path);
			return false;
		}

		error = avcodec_parameters_to_context(codec, stream->codecpar);
		if (error < 0 || (error = avcodec_open2(codec, decoder, nullptr)) < 0)
		{
			IOSOverlayMedia.error("Could not initialize the decoder for '%s': %s", path, utils::av_error_to_string(error));
			return false;
		}

		packet = av_packet_alloc();
		frame = av_frame_alloc();
		if (!packet || !frame)
		{
			IOSOverlayMedia.error("Could not allocate decoder buffers for '%s'", path);
			return false;
		}
		return true;
	}
};

bool wait_until(std::stop_token stop_token, std::chrono::steady_clock::time_point target)
{
	while (!stop_token.stop_requested())
	{
		const auto now = std::chrono::steady_clock::now();
		if (now >= target)
		{
			return true;
		}
		const auto maximum_sleep = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::milliseconds{10});
		std::this_thread::sleep_for(std::min(target - now, maximum_sleep));
	}
	return false;
}
}

class overlay_media_source::implementation
{
public:
	explicit implementation(overlay_media_source& owner)
		: m_owner(owner)
		, m_pcm_buffer(pcm_capacity_samples)
	{
	}

	~implementation()
	{
		set_active(false);
	}

	void set_video_path(const std::string& path)
	{
		std::lock_guard lock{m_control_mutex};
		const bool restart = m_active;
		if (restart)
		{
			stop_locked();
		}
		m_video_path = path;
		m_has_video = !path.empty();
		if (restart)
		{
			start_locked();
		}
	}

	void set_audio_path(const std::string& path)
	{
		std::lock_guard lock{m_control_mutex};
		const bool restart = m_active;
		if (restart)
		{
			stop_locked();
		}
		m_audio_path = path;
		if (restart)
		{
			start_locked();
		}
	}

	void set_active(bool active)
	{
		std::lock_guard lock{m_control_mutex};
		if (m_active == active)
		{
			return;
		}
		if (active)
		{
			start_locked();
		}
		else
		{
			stop_locked();
		}
	}

	bool get_active() const
	{
		return m_active && (!m_has_video || !m_video_failed);
	}

	bool has_new() const
	{
		return m_has_new;
	}

	void get_image(std::vector<u8>& data, int& width, int& height, int& channels, int& bits_per_pixel)
	{
		if (!m_has_new.exchange(false))
		{
			return;
		}

		std::lock_guard lock{m_frame_mutex};
		data = m_frame;
		width = m_frame_width;
		height = m_frame_height;
		channels = m_frame.empty() ? 0 : 4;
		bits_per_pixel = m_frame.empty() ? 0 : 32;
	}

private:
	void start_locked()
	{
		m_active = true;
		m_video_failed = false;
		m_pcm_buffer.clear();

		if (!m_audio_path.empty())
		{
			m_audio_output = std::make_unique<IOSAudioBackend>();
			m_audio_output->SetWriteCallback([this](u32 byte_count, void* buffer)
			{
				return render_audio(byte_count, buffer);
			});
			if (m_audio_output->Open({}, AudioFreq::FREQ_48K, AudioSampleSize::FLOAT, AudioChannelCnt::STEREO, audio_channel_layout::stereo))
			{
				m_audio_thread = std::jthread([this](std::stop_token stop_token)
				{
					decode_audio(stop_token);
				});
				m_audio_output->Play();
				IOSOverlayMedia.notice("Started native overlay audio playback: %s", m_audio_path);
			}
			else
			{
				IOSOverlayMedia.error("Could not open RemoteIO for native overlay audio");
				m_audio_output.reset();
			}
		}

		if (!m_video_path.empty())
		{
			m_video_thread = std::jthread([this](std::stop_token stop_token)
			{
				decode_video(stop_token);
			});
		}
	}

	void stop_locked()
	{
		m_active = false;
		if (m_audio_output)
		{
			m_audio_output->Close();
			m_audio_output.reset();
		}

		if (m_audio_thread.joinable())
		{
			m_audio_thread.request_stop();
			m_audio_thread.join();
		}
		if (m_video_thread.joinable())
		{
			m_video_thread.request_stop();
			m_video_thread.join();
		}

		m_pcm_buffer.clear();
		m_has_new = false;
	}

	u32 render_audio(u32 byte_count, void* buffer) noexcept
	{
		std::memset(buffer, 0, byte_count);
		if (!m_active || byte_count < sizeof(float))
		{
			return byte_count;
		}

		std::span<float> samples{static_cast<float*>(buffer), byte_count / sizeof(float)};
		const std::size_t read = m_pcm_buffer.read(samples);
		if (read)
		{
			AudioBackend::apply_volume_static(audio::get_volume(), static_cast<u32>(read), samples.data(), samples.data());
		}
		return byte_count;
	}

	bool push_audio(std::stop_token stop_token, std::span<const float> samples)
	{
		std::size_t written = 0;
		while (written < samples.size() && !stop_token.stop_requested())
		{
			written += m_pcm_buffer.write(samples.subspan(written));
			if (written < samples.size())
			{
				std::this_thread::sleep_for(std::chrono::milliseconds{2});
			}
		}
		return !stop_token.stop_requested();
	}

	void decode_audio(std::stop_token stop_token)
	{
		while (!stop_token.stop_requested())
		{
			ffmpeg_decoder decoder;
			if (!decoder.open(m_audio_path, AVMEDIA_TYPE_AUDIO))
			{
				return;
			}

			AVChannelLayout input_layout{};
			if (decoder.codec->ch_layout.nb_channels <= 0 || av_channel_layout_copy(&input_layout, &decoder.codec->ch_layout) < 0)
			{
				IOSOverlayMedia.error("Overlay audio stream has no usable channel layout: %s", m_audio_path);
				return;
			}
			if (input_layout.order == AV_CHANNEL_ORDER_UNSPEC)
			{
				const int channel_count = input_layout.nb_channels;
				av_channel_layout_uninit(&input_layout);
				av_channel_layout_default(&input_layout, channel_count);
			}

			SwrContext* resampler = nullptr;
			const AVChannelLayout output_layout = AV_CHANNEL_LAYOUT_STEREO;
			const int input_rate = decoder.codec->sample_rate > 0
				? decoder.codec->sample_rate
				: decoder.stream->codecpar->sample_rate;
			if (input_rate <= 0)
			{
				IOSOverlayMedia.error("Overlay audio stream has no usable sample rate: %s", m_audio_path);
				av_channel_layout_uninit(&input_layout);
				return;
			}
			int error = swr_alloc_set_opts2(
				&resampler,
				&output_layout,
				AV_SAMPLE_FMT_FLT,
				output_sample_rate,
				&input_layout,
				decoder.codec->sample_fmt,
				input_rate,
				0,
				nullptr);
			if (error < 0 || !resampler || (error = swr_init(resampler)) < 0)
			{
				IOSOverlayMedia.error("Could not create the overlay audio resampler for '%s': %s", m_audio_path, utils::av_error_to_string(error));
				if (resampler)
				{
					swr_free(&resampler);
				}
				av_channel_layout_uninit(&input_layout);
				return;
			}

			auto consume_frames = [&]() -> bool
			{
				while (!stop_token.stop_requested())
				{
					const int receive_error = avcodec_receive_frame(decoder.codec, decoder.frame);
					if (receive_error == AVERROR(EAGAIN) || receive_error == AVERROR_EOF)
					{
						return true;
					}
					if (receive_error < 0)
					{
						IOSOverlayMedia.error("Overlay audio decode failed for '%s': %s", m_audio_path, utils::av_error_to_string(receive_error));
						return false;
					}

					const int maximum_frames = static_cast<int>(av_rescale_rnd(
						swr_get_delay(resampler, input_rate) + decoder.frame->nb_samples,
						output_sample_rate,
						input_rate,
						AV_ROUND_UP));
					std::vector<float> converted(static_cast<std::size_t>(maximum_frames) * output_channels);
					u8* output[] = {reinterpret_cast<u8*>(converted.data())};
					const int converted_frames = swr_convert(
						resampler,
						output,
						maximum_frames,
						const_cast<const u8**>(decoder.frame->extended_data),
						decoder.frame->nb_samples);
					if (converted_frames < 0)
					{
						IOSOverlayMedia.error("Overlay audio conversion failed for '%s': %s", m_audio_path, utils::av_error_to_string(converted_frames));
						return false;
					}
					converted.resize(static_cast<std::size_t>(converted_frames) * output_channels);
					if (!push_audio(stop_token, converted))
					{
						return false;
					}
				}
				return false;
			};

			bool succeeded = true;
			int read_error = 0;
			while (!stop_token.stop_requested() && (read_error = av_read_frame(decoder.format, decoder.packet)) >= 0)
			{
				if (decoder.packet->stream_index == decoder.stream_index)
				{
					error = avcodec_send_packet(decoder.codec, decoder.packet);
					if (error < 0 || !consume_frames())
					{
						if (error < 0)
						{
							IOSOverlayMedia.error("Overlay audio packet decode failed for '%s': %s", m_audio_path, utils::av_error_to_string(error));
						}
						succeeded = false;
						av_packet_unref(decoder.packet);
						break;
					}
				}
				av_packet_unref(decoder.packet);
			}
			if (succeeded && !stop_token.stop_requested() && read_error != AVERROR_EOF)
			{
				IOSOverlayMedia.error("Overlay audio read failed for '%s': %s", m_audio_path, utils::av_error_to_string(read_error));
				succeeded = false;
			}

			if (succeeded && !stop_token.stop_requested())
			{
				error = avcodec_send_packet(decoder.codec, nullptr);
				succeeded = error >= 0 && consume_frames();
			}
			swr_free(&resampler);
			av_channel_layout_uninit(&input_layout);
			if (!succeeded)
			{
				return;
			}
		}
	}

	bool publish_video_frame(const AVFrame& source, SwsContext*& scaler)
	{
		const int width = source.width;
		const int height = source.height;
		if (width <= 0 || height <= 0)
		{
			return false;
		}

		scaler = sws_getCachedContext(
			scaler,
			width,
			height,
			static_cast<AVPixelFormat>(source.format),
			width,
			height,
			AV_PIX_FMT_RGBA,
			SWS_BILINEAR,
			nullptr,
			nullptr,
			nullptr);
		if (!scaler)
		{
			return false;
		}

		const int buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, width, height, 1);
		if (buffer_size <= 0)
		{
			return false;
		}

		std::vector<u8> rgba(static_cast<std::size_t>(buffer_size));
		u8* destination[] = {rgba.data()};
		const int lines[] = {width * 4};
		if (sws_scale(scaler, source.data, source.linesize, 0, height, destination, lines) <= 0)
		{
			return false;
		}

		{
			std::lock_guard lock{m_frame_mutex};
			m_frame = std::move(rgba);
			m_frame_width = width;
			m_frame_height = height;
		}
		m_has_new = true;
		m_owner.notify_frame_update();
		return true;
	}

	void decode_video(std::stop_token stop_token)
	{
		while (!stop_token.stop_requested())
		{
			ffmpeg_decoder decoder;
			if (!decoder.open(m_video_path, AVMEDIA_TYPE_VIDEO))
			{
				m_video_failed = true;
				m_owner.notify_frame_update();
				return;
			}

			SwsContext* scaler = nullptr;

			const auto started_at = std::chrono::steady_clock::now();
			s64 first_timestamp = AV_NOPTS_VALUE;
			u64 fallback_frame = 0;
			auto consume_frames = [&]() -> bool
			{
				while (!stop_token.stop_requested())
				{
					const int error = avcodec_receive_frame(decoder.codec, decoder.frame);
					if (error == AVERROR(EAGAIN) || error == AVERROR_EOF)
					{
						return true;
					}
					if (error < 0)
					{
						IOSOverlayMedia.error("Overlay video decode failed for '%s': %s", m_video_path, utils::av_error_to_string(error));
						return false;
					}

					const s64 timestamp = decoder.frame->best_effort_timestamp;
					if (first_timestamp == AV_NOPTS_VALUE && timestamp != AV_NOPTS_VALUE)
					{
						first_timestamp = timestamp;
					}
					double elapsed_seconds = static_cast<double>(fallback_frame++) / 30.0;
					if (timestamp != AV_NOPTS_VALUE && first_timestamp != AV_NOPTS_VALUE)
					{
						elapsed_seconds = static_cast<double>(timestamp - first_timestamp) * av_q2d(decoder.stream->time_base);
					}
					const auto target = started_at + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>{std::max(elapsed_seconds, 0.0)});
					if (!wait_until(stop_token, target))
					{
						return false;
					}
					if (!publish_video_frame(*decoder.frame, scaler))
					{
						IOSOverlayMedia.error("Could not convert an overlay video frame for '%s'", m_video_path);
						return false;
					}
				}
				return false;
			};

			bool succeeded = true;
			int read_error = 0;
			while (!stop_token.stop_requested() && (read_error = av_read_frame(decoder.format, decoder.packet)) >= 0)
			{
				if (decoder.packet->stream_index == decoder.stream_index)
				{
					const int error = avcodec_send_packet(decoder.codec, decoder.packet);
					if (error < 0 || !consume_frames())
					{
						if (error < 0)
						{
							IOSOverlayMedia.error("Overlay video packet decode failed for '%s': %s", m_video_path, utils::av_error_to_string(error));
						}
						succeeded = false;
						av_packet_unref(decoder.packet);
						break;
					}
				}
				av_packet_unref(decoder.packet);
			}
			if (succeeded && !stop_token.stop_requested() && read_error != AVERROR_EOF)
			{
				IOSOverlayMedia.error("Overlay video read failed for '%s': %s", m_video_path, utils::av_error_to_string(read_error));
				succeeded = false;
			}

			if (succeeded && !stop_token.stop_requested())
			{
				const int error = avcodec_send_packet(decoder.codec, nullptr);
				succeeded = error >= 0 && consume_frames();
			}
			sws_freeContext(scaler);
			if (!succeeded)
			{
				m_video_failed = true;
				m_owner.notify_frame_update();
				return;
			}
		}
	}

	overlay_media_source& m_owner;
	mutable std::mutex m_control_mutex;
	std::mutex m_frame_mutex;
	std::string m_video_path;
	std::string m_audio_path;
	std::atomic_bool m_active = false;
	std::atomic_bool m_has_video = false;
	std::atomic_bool m_video_failed = false;
	std::atomic_bool m_has_new = false;
	std::jthread m_audio_thread;
	std::jthread m_video_thread;
	std::unique_ptr<IOSAudioBackend> m_audio_output;
	overlay_pcm_buffer m_pcm_buffer;
	std::vector<u8> m_frame;
	int m_frame_width = 0;
	int m_frame_height = 0;
};

overlay_media_source::overlay_media_source()
	: m_impl(std::make_unique<implementation>(*this))
{
}

overlay_media_source::~overlay_media_source() = default;

void overlay_media_source::set_video_path(const std::string& video_path)
{
	m_impl->set_video_path(video_path);
}

void overlay_media_source::set_audio_path(const std::string& audio_path)
{
	m_impl->set_audio_path(audio_path);
}

void overlay_media_source::set_active(bool active)
{
	m_impl->set_active(active);
}

bool overlay_media_source::get_active() const
{
	return m_impl->get_active();
}

bool overlay_media_source::has_new() const
{
	return m_impl->has_new();
}

void overlay_media_source::get_image(std::vector<u8>& data, int& width, int& height, int& channels, int& bits_per_pixel)
{
	m_impl->get_image(data, width, height, channels, bits_per_pixel);
}

void overlay_media_source::notify_frame_update()
{
	notify_update();
}

std::unique_ptr<video_source> make_overlay_media_source()
{
	return std::make_unique<overlay_media_source>();
}
}
