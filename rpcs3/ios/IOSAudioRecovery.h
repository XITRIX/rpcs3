#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace rpcs3::ios::audio_detail
{
struct refill_result
{
	std::uint32_t frames = 0;
	bool held = false;
	bool started = false;
	bool resumed = false;
	bool timed_out = false;
};

// Owned by the ring's reader; configure before installing the callback and reset
// on the reader (or while the callback is stopped).
// AudioUnit keeps running. Waiting is expressed as silent output frames, never
// a sleep or a wait on the real-time thread.
class refill_controller final
{
public:
	void configure(std::uint32_t rate, std::uint32_t target, std::uint32_t capacity, bool enabled) noexcept
	{
		m_target = std::min(target, capacity);
		m_capacity = capacity;
		m_wait_limit = rate / 10; // At most 100 ms of refill silence per attempt.
		m_cooldown_length = rate / 4; // At least 250 ms of normal reads between attempts.
		m_rearm_length = rate; // A failed refill needs one second of healthy output before retrying.
		m_enabled = enabled && m_target != 0 && m_wait_limit != 0;
		reset();
	}

	void reset() noexcept
	{
		m_refilling = false;
		m_has_audio = false;
		m_short_reads = 0;
		m_waited = 0;
		m_cooldown = 0;
		m_rearm_remaining = 0;
	}

	template <typename Read>
	refill_result read(std::uint32_t requested, std::uint32_t available, Read&& consume) noexcept
	{
		refill_result result;
		if (!requested) return result;
		const bool cooling_down = m_cooldown != 0;
		m_cooldown -= std::min(m_cooldown, requested);

		if (m_refilling)
		{
			const auto threshold = std::min(m_capacity, std::max(m_target, requested));
			if (available < threshold && requested <= m_wait_limit - m_waited)
			{
				m_waited += requested;
				result.held = true;
				return result;
			}
			result.resumed = true;
			result.timed_out = available < threshold;
			if (result.timed_out) m_rearm_remaining = m_rearm_length;
			m_refilling = false;
			m_cooldown = m_cooldown_length;
			m_short_reads = 0;
		}

		result.frames = std::min(requested, consume(requested));
		m_has_audio |= result.frames != 0;
		if (m_rearm_remaining)
		{
			// Repeated refills cannot cure a persistently slow producer. Keep
			// delivering its available PCM until it sustains healthy playback.
			if (result.frames == requested) m_rearm_remaining -= std::min(m_rearm_remaining, requested);
			else m_rearm_remaining = m_rearm_length;
		}
		if (result.frames == requested || cooling_down || result.resumed)
		{
			m_short_reads = 0;
		}
		else if (m_enabled && !m_rearm_remaining && m_has_audio && ++m_short_reads >= 2)
		{
			m_refilling = true;
			m_waited = 0;
			m_short_reads = 0;
			result.started = true;
		}
		return result;
	}

private:
	std::uint32_t m_target = 0, m_capacity = 0, m_wait_limit = 0, m_cooldown_length = 0;
	std::uint32_t m_short_reads = 0, m_waited = 0, m_cooldown = 0;
	std::uint32_t m_rearm_length = 0, m_rearm_remaining = 0;
	bool m_enabled = false, m_refilling = false, m_has_audio = false;
};

// Smooth only discontinuities; complete, steady-state callbacks pass through.
// Both the fade and the memory touched are bounded by the supplied frame count.
class stereo_fader final
{
public:
	void reset(std::uint32_t rate) noexcept
	{
		m_length = std::max<std::uint32_t>(1, rate * 3 / 1000); // 3 ms
		m_remaining = 0;
		m_gap = true;
		m_last = {};
		m_origin = {};
	}

	template <typename Sample>
	void process(Sample* output, std::uint32_t requested, std::uint32_t written) noexcept
	{
		written = std::min(written, requested);
		if (written)
		{
			if (m_gap)
			{
				m_origin = m_last;
				m_remaining = m_length;
				m_gap = false;
			}
			const auto blend_frames = std::min(written, m_remaining);
			for (std::uint32_t frame = 0; frame < blend_frames; ++frame)
			{
				const float weight = static_cast<float>(--m_remaining) / m_length;
				for (std::uint32_t ch = 0; ch < 2; ++ch)
				{
					auto& sample = output[frame * 2 + ch];
					sample = static_cast<Sample>(m_origin[ch] * weight + static_cast<float>(sample) * (1.0f - weight));
				}
			}
			for (std::uint32_t ch = 0; ch < 2; ++ch) m_last[ch] = static_cast<float>(output[(written - 1) * 2 + ch]);
		}

		if (written == requested) return;
		if (!m_gap)
		{
			m_origin = m_last;
			m_remaining = m_length;
			m_gap = true;
		}
		const auto fade_frames = std::min(requested - written, m_remaining);
		for (std::uint32_t frame = written; frame < written + fade_frames; ++frame)
		{
			const float weight = static_cast<float>(--m_remaining) / m_length;
			for (std::uint32_t ch = 0; ch < 2; ++ch)
			{
				auto& sample = output[frame * 2 + ch];
				sample = static_cast<Sample>(m_origin[ch] * weight);
				m_last[ch] = static_cast<float>(sample);
			}
		}
		const auto silence_start = written + fade_frames;
		if (silence_start < requested)
		{
			std::memset(output + silence_start * 2, 0, (requested - silence_start) * 2 * sizeof(Sample));
			m_last = {};
		}
	}

private:
	std::array<float, 2> m_last{}, m_origin{};
	std::uint32_t m_length = 1, m_remaining = 0;
	bool m_gap = true;
};
}
