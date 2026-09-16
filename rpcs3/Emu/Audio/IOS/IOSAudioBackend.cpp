#include "stdafx.h"
#include "IOSAudioBackend.h"
#include "ios/IOSAudioBufferContract.h"
#include "Emu/Cell/timers.hpp"

#include <algorithm>
#include <cstring>

LOG_CHANNEL(IOSAudio, "iOS Audio");

namespace
{
bool check_status(OSStatus status, std::string_view operation)
{
	if (status == noErr)
	{
		return true;
	}

	IOSAudio.error("%s failed with OSStatus %d", operation, status);
	return false;
}
}

IOSAudioBackend::IOSAudioBackend()
	: AudioBackend()
{
	AudioComponentDescription description{};
	description.componentType = kAudioUnitType_Output;
	description.componentSubType = kAudioUnitSubType_RemoteIO;
	description.componentManufacturer = kAudioUnitManufacturer_Apple;
	m_component = AudioComponentFindNext(nullptr, &description);

	if (m_component)
	{
		IOSAudio.notice("RemoteIO output component is available");
	}
	else
	{
		IOSAudio.error("RemoteIO output component is unavailable");
	}
}

IOSAudioBackend::~IOSAudioBackend()
{
	Close();
}

bool IOSAudioBackend::Initialized()
{
	return m_component != nullptr;
}

bool IOSAudioBackend::Operational()
{
	log_diagnostics(false);
	return m_operational.load(std::memory_order_acquire);
}

bool IOSAudioBackend::Open(
	std::string_view dev_id,
	AudioFreq freq,
	AudioSampleSize sample_size,
	AudioChannelCnt ch_cnt,
	audio_channel_layout layout)
{
	std::lock_guard control_lock{m_control_mutex};
	close_unlocked();

	if (!m_component)
	{
		IOSAudio.error("Open() called without a RemoteIO component");
		return false;
	}

	if (!dev_id.empty() && dev_id != "@@@default@@@")
	{
		IOSAudio.warning("Ignoring unsupported iOS audio device id '%s' and using the active system route", dev_id);
	}

	m_sampling_rate = freq;
	m_sample_size = sample_size;
	setup_channel_layout(static_cast<u32>(ch_cnt), output_channel_count, layout, IOSAudio);

	const u32 bytes_per_frame = get_channels() * get_sample_size();
	if (bytes_per_frame == 0 || bytes_per_frame > sizeof(float) * output_channel_count)
	{
		IOSAudio.error("Invalid RemoteIO frame size %u", bytes_per_frame);
		return false;
	}

	AudioUnit unit = nullptr;
	if (!check_status(AudioComponentInstanceNew(m_component, &unit), "AudioComponentInstanceNew") || !unit)
	{
		return false;
	}

	bool initialized = false;
	auto dispose_unit = [&]()
	{
		if (initialized)
		{
			AudioUnitUninitialize(unit);
		}
		AudioComponentInstanceDispose(unit);
	};

	AudioStreamBasicDescription stream{};
	stream.mSampleRate = static_cast<Float64>(get_sampling_rate());
	stream.mFormatID = kAudioFormatLinearPCM;
	stream.mFormatFlags = static_cast<AudioFormatFlags>(kAudioFormatFlagIsPacked) |
		static_cast<AudioFormatFlags>(kAudioFormatFlagsNativeEndian);
	stream.mFormatFlags |= sample_size == AudioSampleSize::FLOAT
		? static_cast<AudioFormatFlags>(kAudioFormatFlagIsFloat)
		: static_cast<AudioFormatFlags>(kAudioFormatFlagIsSignedInteger);
	stream.mBytesPerPacket = bytes_per_frame;
	stream.mFramesPerPacket = 1;
	stream.mBytesPerFrame = bytes_per_frame;
	stream.mChannelsPerFrame = get_channels();
	stream.mBitsPerChannel = get_sample_size() * 8;

	if (!check_status(AudioUnitSetProperty(
		unit,
		kAudioUnitProperty_StreamFormat,
		kAudioUnitScope_Input,
		0,
		&stream,
		sizeof(stream)), "AudioUnitSetProperty(StreamFormat)"))
	{
		dispose_unit();
		return false;
	}

	AURenderCallbackStruct callback{};
	callback.inputProc = &IOSAudioBackend::render_callback;
	callback.inputProcRefCon = this;
	if (!check_status(AudioUnitSetProperty(
		unit,
		kAudioUnitProperty_SetRenderCallback,
		kAudioUnitScope_Input,
		0,
		&callback,
		sizeof(callback)), "AudioUnitSetProperty(RenderCallback)"))
	{
		dispose_unit();
		return false;
	}

	if (!check_status(AudioUnitInitialize(unit), "AudioUnitInitialize"))
	{
		dispose_unit();
		return false;
	}
	initialized = true;

	{
		std::lock_guard callback_lock{m_cb_mutex};
		m_unit = unit;
		m_bytes_per_frame = bytes_per_frame;
		m_fader.reset(get_sampling_rate());
		m_playing = false;
	}
	m_needs_fade_reset.store(false, std::memory_order_relaxed);
	m_operational.store(true, std::memory_order_release);

	IOSAudio.notice(
		"Opened RemoteIO output at %u Hz, %u-bit, %u channels (%s)",
		get_sampling_rate(),
		get_sample_size() * 8,
		get_channels(),
		get_channel_layout());
	return true;
}

void IOSAudioBackend::Close()
{
	std::lock_guard control_lock{m_control_mutex};
	close_unlocked();
}

void IOSAudioBackend::close_unlocked()
{
	m_operational.store(false, std::memory_order_release);

	AudioUnit unit = m_unit;
	if (unit)
	{
		check_status(AudioOutputUnitStop(unit), "AudioOutputUnitStop");
		check_status(AudioUnitUninitialize(unit), "AudioUnitUninitialize");
		check_status(AudioComponentInstanceDispose(unit), "AudioComponentInstanceDispose");
	}

	m_diagnostics.reset_timing();
	log_diagnostics(true);
	std::lock_guard callback_lock{m_cb_mutex};
	m_unit = nullptr;
	m_bytes_per_frame = 0;
	m_fader.reset(get_sampling_rate());
	m_playing = false;
}

f64 IOSAudioBackend::GetCallbackFrameLen()
{
	const u32 sampling_rate = get_sampling_rate();
	return sampling_rate
		? static_cast<f64>(nominal_callback_frames) / sampling_rate
		: static_cast<f64>(nominal_callback_frames) / static_cast<f64>(DEFAULT_AUDIO_SAMPLING_RATE);
}

bool IOSAudioBackend::IsPlaying()
{
	std::lock_guard callback_lock{m_cb_mutex};
	return m_playing;
}

void IOSAudioBackend::Play()
{
	std::lock_guard control_lock{m_control_mutex};
	if (!m_unit || !m_operational.load(std::memory_order_acquire))
	{
		IOSAudio.error("Play() called without an operational RemoteIO unit");
		return;
	}

	{
		std::lock_guard callback_lock{m_cb_mutex};
		if (m_playing)
		{
			return;
		}
		m_diagnostics.reset_timing();
		m_playing = true;
	}

	if (!check_status(AudioOutputUnitStart(m_unit), "AudioOutputUnitStart"))
	{
		{
			std::lock_guard callback_lock{m_cb_mutex};
			m_playing = false;
			m_fader.reset(get_sampling_rate());
		}
		m_operational.store(false, std::memory_order_release);
		notify_error();
		return;
	}

	IOSAudio.notice("RemoteIO playback started");
}

void IOSAudioBackend::Pause()
{
	std::lock_guard control_lock{m_control_mutex};
	if (!m_unit)
	{
		return;
	}

	{
		std::lock_guard callback_lock{m_cb_mutex};
		if (!m_playing)
		{
			return;
		}
	}

	const bool stopped = check_status(AudioOutputUnitStop(m_unit), "AudioOutputUnitStop");
	{
		std::lock_guard callback_lock{m_cb_mutex};
		m_playing = false;
		m_fader.reset(get_sampling_rate());
	}

	if (!stopped)
	{
		m_operational.store(false, std::memory_order_release);
		notify_error();
	}
}

void IOSAudioBackend::log_diagnostics(bool force)
{
	// Operational() is polled by cellAudio, never by the render callback.
	std::unique_lock lock{m_diagnostics_log_mutex, std::try_to_lock};
	if (!lock) return;
	const u64 now = get_system_time();
	if (!force && now < m_next_diagnostics_us) return;
	m_next_diagnostics_us = now + 2'000'000;
	const auto d = m_diagnostics.snapshot();
	if (d.callbacks == m_reported_callbacks) return;
	m_reported_callbacks = d.callbacks;
	IOSAudio.notice("RemoteIO diagnostics: cumulative callbacks=%llu requested_frames=%llu delivered_frames=%llu filler_frames=%llu short_reads=%llu zero_reads=%llu inactive=%llu lock_misses=%llu callback_frames=%llu..%llu max_gap_us=%llu late_callbacks=%llu",
		d.callbacks, d.requested, d.delivered, d.filler, d.short_reads, d.zero_reads, d.inactive, d.lock_misses,
		d.min_frames, d.max_frames, d.max_gap_us, d.late_callbacks);
}

void IOSAudioBackend::notify_error()
{
	std::lock_guard state_lock{m_state_cb_mutex};
	if (m_state_callback)
	{
		m_state_callback(AudioStateEvent::UNSPECIFIED_ERROR);
	}
}

OSStatus IOSAudioBackend::render_callback(
	void* user_data,
	AudioUnitRenderActionFlags*,
	const AudioTimeStamp*,
	UInt32,
	UInt32 frame_count,
	AudioBufferList* output_data) noexcept
{
	auto* backend = static_cast<IOSAudioBackend*>(user_data);
	if (!backend || !output_data)
	{
		return kAudio_ParamError;
	}

	if (output_data->mNumberBuffers != 1 || !output_data->mBuffers[0].mData)
	{
		for (UInt32 index = 0; index < output_data->mNumberBuffers; index++)
		{
			AudioBuffer& buffer = output_data->mBuffers[index];
			if (buffer.mData && buffer.mDataByteSize)
			{
				std::memset(buffer.mData, 0, buffer.mDataByteSize);
			}
		}
		return noErr;
	}

	AudioBuffer& buffer = output_data->mBuffers[0];
	const u32 bytes_per_frame = backend->m_bytes_per_frame;
	u32 requested = rpcs3::ios::audio::callback_byte_count(
		frame_count,
		bytes_per_frame,
		buffer.mDataByteSize);
	requested = rpcs3::ios::audio::aligned_written_byte_count(
		requested,
		requested,
		bytes_per_frame);
	buffer.mDataByteSize = requested;

	if (requested == 0)
	{
		return noErr;
	}

	auto* output = static_cast<u8*>(buffer.mData);
	const u32 requested_frames = requested / bytes_per_frame;
	const u64 callback_time = get_system_time();
	using rpcs3::ios::audio_detail::callback_outcome;
	std::unique_lock callback_lock{backend->m_cb_mutex, std::defer_lock};
	if (!callback_lock.try_lock())
	{
		std::memset(output, 0, requested);
		backend->m_needs_fade_reset.store(true, std::memory_order_relaxed);
		backend->m_diagnostics.record(requested_frames, 0, callback_outcome::lock_miss, callback_time, backend->get_sampling_rate());
		return noErr;
	}
	if (!backend->m_write_callback || !backend->m_playing)
	{
		std::memset(output, 0, requested);
		backend->m_diagnostics.record(requested_frames, 0, callback_outcome::inactive, callback_time, backend->get_sampling_rate());
		return noErr;
	}

	u32 written = backend->m_write_callback(requested, output);
	written = rpcs3::ios::audio::aligned_written_byte_count(written, requested, bytes_per_frame);
	if (backend->m_needs_fade_reset.exchange(false, std::memory_order_relaxed))
	{
		backend->m_fader.reset(backend->get_sampling_rate());
	}
	if (backend->get_convert_to_s16())
	{
		backend->m_fader.process(reinterpret_cast<s16*>(output), requested_frames, written / bytes_per_frame);
	}
	else
	{
		backend->m_fader.process(reinterpret_cast<f32*>(output), requested_frames, written / bytes_per_frame);
	}
	backend->m_diagnostics.record(requested_frames, written / bytes_per_frame, callback_outcome::rendered, callback_time, backend->get_sampling_rate());
	return noErr;
}
