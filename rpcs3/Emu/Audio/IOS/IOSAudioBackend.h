#pragma once

#include "Emu/Audio/AudioBackend.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#include <AudioToolbox/AudioComponent.h>
#include <AudioToolbox/AudioOutputUnit.h>
#include <AudioToolbox/AudioUnitProperties.h>
#pragma clang diagnostic pop

#include <array>
#include <atomic>
#include <mutex>

class IOSAudioBackend final : public AudioBackend
{
public:
	IOSAudioBackend();
	~IOSAudioBackend() override;

	IOSAudioBackend(const IOSAudioBackend&) = delete;
	IOSAudioBackend& operator=(const IOSAudioBackend&) = delete;

	std::string_view GetName() const override { return "iOS RemoteIO"; }

	bool Initialized() override;
	bool Operational() override;
	bool Open(std::string_view dev_id, AudioFreq freq, AudioSampleSize sample_size, AudioChannelCnt ch_cnt, audio_channel_layout layout) override;
	void Close() override;

	f64 GetCallbackFrameLen() override;
	bool IsPlaying() override;
	void Play() override;
	void Pause() override;

private:
	static constexpr u32 output_channel_count = 2;
	static constexpr u32 nominal_callback_frames = 512;

	static OSStatus render_callback(
		void* user_data,
		AudioUnitRenderActionFlags* action_flags,
		const AudioTimeStamp* timestamp,
		UInt32 bus_number,
		UInt32 frame_count,
		AudioBufferList* output_data) noexcept;

	void close_unlocked();
	void notify_error();

	std::mutex m_control_mutex;
	AudioComponent m_component = nullptr;
	AudioUnit m_unit = nullptr;
	std::array<u8, sizeof(float) * output_channel_count> m_last_frame{};
	u32 m_bytes_per_frame = 0;
	std::atomic_bool m_operational = false;
};
