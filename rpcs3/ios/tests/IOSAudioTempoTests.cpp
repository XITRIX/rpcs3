#include "ios/IOSAudioTempo.h"
#include "ios/IOSAudioRecovery.h"
#include <SoundTouch.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <deque>
#include <limits>
#include <vector>

using namespace rpcs3::ios::audio_detail;

struct measurement
{
	std::uint64_t requested = 0, delivered = 0, nonzero = 0, refills = 0, timeouts = 0;
	std::uint64_t crossings = 0, advances = 0, max_advance_gap_us = 0;
	double min_tempo = 1, max_tempo = 0, max_step = 0, max_queue_ms = 0, energy = 0;
	double coverage() const { return static_cast<double>(delivered) / requested; }
	double rms() const { return std::sqrt(energy / requested); }
	double crossings_per_second() const { return crossings * 48000.0 / requested; }
};

// Execute the actual bundled DSP plus production tempo/refill/fade helpers.
// Guest arrivals and 5.33 ms producer / 512-frame device ticks are synthetic;
// this fixture does not emulate a PS3 or predict DiRT 2's scheduling.
measurement simulate(double speed, bool measured, bool insert_missing_silence,
	bool variable = false, bool guest_silence = false, unsigned channels = 2, bool coupled = false)
{
	soundtouch::SoundTouch dsp;
	dsp.setSetting(SETTING_SEQUENCE_MS, 40);
	dsp.setSetting(SETTING_SEEKWINDOW_MS, 15);
	dsp.setSetting(SETTING_OVERLAP_MS, 8);
	dsp.setSetting(SETTING_USE_QUICKSEEK, 0);
	dsp.setSetting(SETTING_USE_AA_FILTER, 1);
	dsp.setSampleRate(48000);
	dsp.setChannels(channels);
	tempo_controller control;
	refill_controller refill;
	refill.configure(48000, 1632, 2592, true);
	stereo_fader fade;
	fade.reset(48000);
	std::deque<float> ring;
	std::vector<float> source(channels * 256), batch(channels * 2592);
	std::vector<float> output(512 * 2);
	double tempo = 1, next_producer = 0, next_callback = 0, source_credit = 0;
	std::uint64_t source_frames = 0;
	measurement result;
	float previous_sample = 0;
	std::uint64_t last_advance = 0;
	double average_queue_us = 34000;
	bool untouched_expected = false;
	for (int i = 0; i < 9; ++i) dsp.putSamples(source.data(), 256); // Normal restart prefill.

	for (std::uint64_t us = 0; us < 15'000'000; us += 1000)
	{
		double incoming = speed;
		if (variable)
		{
			incoming = us < 3'000'000 || us >= 7'000'000 ? 1.0 : 0.25;
			if (us >= 5'000'000 && us < 6'000'000) incoming = 0;
		}
		source_credit += 48 * incoming;
		const auto queued_frames = ring.size() + dsp.numSamples();
		const double queued_us = queued_frames * 1'000'000.0 / 48000;
		result.max_queue_ms = std::max(result.max_queue_ms, queued_us / 1000);
		double next = tempo;
		if (measured)
		{
			next = control.update(us, queued_us, 34000 * 0.75, 48000);
		}
		else
		{
			const double normalized = queued_us / 36666.5 / 0.75;
			if (normalized < 1)
			{
				const auto step = (normalized + tempo) / 2;
				if (std::abs(step - tempo) > 0.1) next = std::clamp(step, 0.1, 1.0);
			}
			else next = 1;
		}
		if (std::abs(next - tempo) > 0.001)
		{
			result.max_step = std::max(result.max_step, std::abs(next - tempo));
			tempo = next;
			dsp.setTempo(tempo);
		}

		bool advance_due = us >= next_producer;
		if (coupled)
		{
			double target = 36666.5 / std::clamp(average_queue_us / 512, 0.25, 1.0);
			const double ratio = queued_us / target;
			double period = ratio >= 1 ? 6399 - (6399 - 5333) / ratio : 2666 + (5333 - 2666) * ratio * ratio;
			const bool missing = source_credit < 256;
			if (missing) period = std::max(period, 5333.0);
			advance_due = us - last_advance >= period;
			if (missing && !untouched_expected && us - last_advance <= 10666) advance_due = false;
		}
		if (advance_due)
		{
			const bool missing = source_credit < 256;
			if (!missing)
			{
				for (unsigned f = 0; f < 256; ++f)
				{
					const float sample = guest_silence ? 0 : 0.25 * std::sin(2 * 3.141592653589793 * 440 * (source_frames + f) / 48000.0);
					for (unsigned ch = 0; ch < channels; ++ch) source[f * channels + ch] = sample;
				}
				dsp.putSamples(source.data(), 256);
				control.record_input(256);
				source_credit -= 256;
				source_frames += 256;
			}
			else if (insert_missing_silence)
			{
				std::fill(source.begin(), source.end(), 0);
				dsp.putSamples(source.data(), 256);
			}
			const auto count = dsp.receiveSamples(batch.data(), static_cast<unsigned>(2592 - ring.size()));
			for (unsigned f = 0; f < count; ++f)
			{
				for (unsigned ch = 0; ch < channels; ++ch) assert(std::isfinite(batch[f * channels + ch]));
				ring.push_back(batch[f * channels]);
			}
			// Like cellAudio, drain on producer advancement, not each polling loop.
			next_producer += 256'000'000.0 / 48000;
			average_queue_us = 0.98 * average_queue_us + 0.02 * ((ring.size() + dsp.numSamples()) * 1'000'000.0 / 48000);
			untouched_expected = missing;
			++result.advances;
			result.max_advance_gap_us = std::max(result.max_advance_gap_us, us - last_advance);
			last_advance = us;
		}

		if (us >= next_callback)
		{
			const auto read = refill.read(512, static_cast<unsigned>(ring.size()), [&](unsigned request)
			{
				const auto n = std::min<unsigned>(request, ring.size());
				for (unsigned f = 0; f < n; ++f)
				{
					output[f * 2] = output[f * 2 + 1] = ring.front();
					ring.pop_front();
				}
				return n;
			});
			fade.process(output.data(), 512, read.frames);
			result.refills += read.started;
			result.timeouts += read.timed_out;
			if (us >= (variable ? 11'000'000 : 3'000'000))
			{
				result.requested += 512;
				result.delivered += read.frames;
				result.min_tempo = std::min(result.min_tempo, tempo);
				result.max_tempo = std::max(result.max_tempo, tempo);
				for (unsigned f = 0; f < 512; ++f)
				{
					assert(std::isfinite(output[f * 2]));
					assert(std::abs(output[f * 2]) <= 0.251);
					result.energy += output[f * 2] * output[f * 2];
					result.nonzero += std::abs(output[f * 2]) > 0.001;
					result.crossings += previous_sample <= 0 && output[f * 2] > 0;
					previous_sample = output[f * 2];
				}
			}
			next_callback += 512'000'000.0 / 48000;
		}
	}
	std::printf("speed=%.2f measured=%d filler=%d variable=%d silent=%d ch=%u coupled=%d coverage=%.3f rms=%.4f positive_crossings_per_s=%.1f tempo=%.3f..%.3f max_step=%.3f max_queue_ms=%.1f refills=%llu timeouts=%llu\n",
		speed, measured, insert_missing_silence, variable, guest_silence, channels, coupled, result.coverage(), result.rms(), result.crossings_per_second(), result.min_tempo, result.max_tempo,
		result.max_step, result.max_queue_ms, static_cast<unsigned long long>(result.refills), static_cast<unsigned long long>(result.timeouts));
	return result;
}

void test_controller()
{
	tempo_controller c;
	assert(c.update(0, 0, 25000, 48000) == 1);
	for (unsigned us = 1; us < 20000; ++us) assert(c.update(us, 0, 25000, 48000) == 1);
	for (std::uint64_t us = 20000; us <= 3'000'000; us += 20000)
	{
		c.record_input(480); // Half real time, including legitimate all-zero PCM.
		const auto tempo = c.update(us, 25000, 25000, 48000);
		assert(std::isfinite(tempo) && tempo >= 0.1 && tempo <= 1);
	}
	assert(std::abs(c.input_rate() - 0.5) < 0.001);
	assert(c.update(3'000'001, 0, 25000, 48000) == c.update(3'000'001, 50000, 25000, 48000));
	c.reset();
	assert(c.update(10, 0, 25000, 48000) == 1);
	assert(c.update(1, 0, 25000, 48000) == 1); // Clock reversal.
	assert(c.update(2'000'000, 0, 25000, 48000) == 1); // Suspended producer.
	for (double invalid : {-1.0, 0.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
	{
		assert(c.update(20, 0, invalid, 48000) == 1);
		assert(c.update(20, 0, 25000, invalid) == 1);
	}
}

int main()
{
	test_controller();
	const auto silence_filled = simulate(0.2, false, true);
	const auto missing = simulate(0.2, false, false);
	for (double speed : {1.0, 0.75, 0.5, 0.2, 0.16})
	{
		const auto stable = simulate(speed, true, false);
		assert(stable.coverage() > 0.99);
		assert(stable.rms() > 0.16);
		assert(stable.max_step < 0.101);
		assert(stable.max_queue_ms < 100);
		assert(stable.crossings_per_second() > 430 && stable.crossings_per_second() < 450);
		if (speed == 0.2)
		{
			assert(stable.rms() > silence_filled.rms() * 1.5);
			assert(stable.coverage() > missing.coverage() + 0.2);
			assert(stable.max_tempo - stable.min_tempo < 0.05);
		}
	}
	const auto silent = simulate(0.5, true, false, false, true);
	assert(silent.coverage() > 0.99 && silent.energy == 0);
	assert(silent.min_tempo > 0.45);
	const auto multichannel = simulate(0.2, true, false, false, false, 8);
	assert(multichannel.coverage() > 0.99 && multichannel.rms() > 0.16);
	const auto changing = simulate(1, true, false, true);
	assert(changing.coverage() > 0.99);
	assert(changing.max_queue_ms < 300);
	const auto coupled = simulate(0.2, true, false, false, false, 2, true);
	assert(coupled.coverage() > 0.99 && coupled.rms() > 0.16);
	assert(coupled.max_advance_gap_us <= 12000);
	assert(coupled.advances > 1500); // Missing buffers do not get a fresh 42 ms wait.
	const auto coupled_recovery = simulate(1, true, false, true, false, 2, true);
	assert(coupled_recovery.coverage() > 0.99 && coupled_recovery.rms() > 0.16);
	assert(coupled_recovery.max_queue_ms < 300);
	assert(coupled_recovery.max_advance_gap_us <= 12000);
}
