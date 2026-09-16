#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rpcs3::ios::audio_detail
{
// Producer-thread state. SoundTouch emits batches, so an empty output FIFO is
// not an instantaneous measurement of guest speed. Estimate incoming PCM over
// time, use filtered queue occupancy for a small correction, and slew tempo.
class tempo_controller final
{
public:
	static constexpr double minimum_tempo = 0.1;
	static constexpr double maximum_tempo = 1.0;

	void reset() noexcept { *this = {}; }
	// Count mixed guest frames, including guest-written silence. Never count
	// missing-buffer filler or restart prefill as newly produced guest audio.
	void record_input(std::uint32_t frames) noexcept { m_input_frames += frames; }

	double update(std::uint64_t now_us, double queue_us, double target_us, double sample_rate) noexcept
	{
		if (!std::isfinite(queue_us) || !std::isfinite(target_us) || !std::isfinite(sample_rate) ||
			queue_us < 0 || target_us <= 0 || sample_rate <= 0)
		{
			reset();
			return m_tempo;
		}
		if (!m_started)
		{
			m_started = true;
			m_last_update = m_last_measure = now_us;
			m_queue_us = queue_us;
			return m_tempo;
		}
		if (now_us < m_last_update || now_us - m_last_update > 1'000'000)
		{
			reset();
			return update(now_us, queue_us, target_us, sample_rate);
		}

		const auto elapsed = now_us - m_last_update;
		if (elapsed < 20'000) return m_tempo;
		m_last_update = now_us;
		const double queue_weight = static_cast<double>(elapsed) / (100'000.0 + elapsed);
		m_queue_us += queue_weight * (queue_us - m_queue_us);

		const auto measured = now_us - m_last_measure;
		if (measured >= 100'000)
		{
			const double incoming = std::clamp(static_cast<double>(m_input_frames) * 1'000'000.0 / sample_rate / measured, 0.0, 2.0);
			// Recover quickly from a substantial increase in available PCM.
			// Keep ordinary one-block measurement jitter on the slower filter.
			const bool recovered = incoming > m_input_rate + std::max(0.1, m_input_rate * 0.5);
			const double weight = static_cast<double>(measured) / ((recovered ? 50'000.0 : 250'000.0) + measured);
			m_input_rate += weight * (incoming - m_input_rate);
			m_input_frames = 0;
			m_last_measure = now_us;
		}

		const double queue_error = std::clamp((m_queue_us - target_us) / target_us, -1.0, 1.0);
		const double rate_tempo = m_input_rate * (1.0 + 0.15 * queue_error);
		// A recovered producer can otherwise build seconds of DSP output
		// while the rate estimate catches up. Sustained excess queue wins
		// over that estimate; filtering keeps individual batches harmless.
		const double backlog_tempo = (m_queue_us - target_us) / (3.0 * target_us);
		const double desired = std::clamp(std::max(rate_tempo, backlog_tempo), minimum_tempo, maximum_tempo);
		// A 20 ms update changes tempo by at most -0.02 / +0.10. In
		// particular, a newly emitted DSP batch cannot jump straight to 1x.
		const double step = std::clamp(desired - m_tempo,
			-static_cast<double>(elapsed) / 1'000'000.0, static_cast<double>(elapsed) / 200'000.0);
		m_tempo = std::clamp(m_tempo + step, minimum_tempo, maximum_tempo);
		return m_tempo;
	}

	double input_rate() const noexcept { return m_input_rate; }

private:
	std::uint64_t m_last_update = 0, m_last_measure = 0, m_input_frames = 0;
	double m_input_rate = 1, m_tempo = 1, m_queue_us = 0;
	bool m_started = false;
};
}
