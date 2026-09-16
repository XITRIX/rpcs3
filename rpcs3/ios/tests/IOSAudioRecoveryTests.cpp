#include "ios/IOSAudioRecovery.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
#include <type_traits>

using namespace rpcs3::ios::audio_detail;

template <typename Sample>
void test_fades(std::uint32_t rate)
{
	stereo_fader fader;
	fader.reset(rate);
	const std::uint32_t frames = rate / 100;
	const Sample amplitude = std::is_same_v<Sample, float> ? Sample{1} : Sample{30000};
	std::vector<Sample> output(frames * 2 + 2, Sample{123});
	for (std::uint32_t i = 0; i < frames; ++i) { output[i * 2] = amplitude; output[i * 2 + 1] = -amplitude; }
	fader.process(output.data(), frames, frames);
	assert(output[0] > 0 && output[0] < amplitude);
	assert(output[(frames - 1) * 2] == amplitude);
	assert(output[frames * 2] == Sample{123});

	// The fade out persists across short callbacks, then becomes exact silence.
	Sample previous = amplitude;
	for (std::uint32_t i = 0; i < rate / 100; ++i)
	{
		std::array<Sample, 4> short_output{Sample{123}, Sample{123}, Sample{123}, Sample{123}};
		fader.process(short_output.data(), 1, 0);
		assert(short_output[0] <= previous && short_output[0] >= 0);
		assert(short_output[1] == -short_output[0]);
		assert(short_output[2] == Sample{123} && short_output[3] == Sample{123});
		previous = short_output[0];
	}
	assert(previous == 0);
	std::fill(output.begin(), output.end(), -amplitude);
	fader.process(output.data(), frames, frames);
	assert(output[0] < 0 && output[0] > -amplitude);
	assert(output[(frames - 1) * 2] == -amplitude);

	// A shortage shorter than the ramp crossfades from the last emitted value.
	std::array<Sample, 2> tiny{};
	fader.process(tiny.data(), 1, 0);
	const auto last = tiny[0];
	tiny = {amplitude, amplitude};
	fader.process(tiny.data(), 1, 1);
	assert(tiny[0] > last && tiny[0] < 0);

	// Mixed data and filler in one callback, including the smallest buffer.
	for (std::uint32_t size : {1u, 2u, 63u, 256u, 512u, 2048u})
	{
		for (std::uint32_t written : {0u, size / 2, size})
		{
			std::vector<Sample> guarded(size * 2 + 4, Sample{123});
			for (std::uint32_t i = 0; i < written * 2; ++i) guarded[2 + i] = amplitude;
			fader.process(guarded.data() + 2, size, written);
			assert(guarded[0] == Sample{123} && guarded[1] == Sample{123});
			assert(guarded[size * 2 + 2] == Sample{123} && guarded[size * 2 + 3] == Sample{123});
		}
	}
	fader.reset(rate);
	std::fill(output.begin(), output.end(), amplitude);
	fader.process(output.data(), frames, 0);
	for (std::uint32_t i = 0; i < frames * 2; ++i) assert(output[i] == 0);
}

void test_refill()
{
	refill_controller controller;
	controller.configure(48000, 1632, 2592, true);
	std::uint32_t available = 0, calls = 0;
	auto read = [&](std::uint32_t request)
	{
		++calls;
		const auto used = std::min(request, available);
		available -= used;
		return used;
	};
	auto tick = [&] { return controller.read(512, available, read); };
	for (int i = 0; i < 10; ++i) assert(!tick().started); // No refill loop during empty startup.
	available = 2048;
	assert(tick().frames == 512);
	available = 128;
	assert(!tick().started);
	available = 512;
	assert(!tick().started); // A successful callback breaks the underrun streak.
	available = 128;
	assert(!tick().started);
	assert(tick().started);
	const auto before = calls;
	available = 1000;
	assert(tick().held && available == 1000 && calls == before);
	available = 1700;
	const auto resumed = tick();
	assert(resumed.resumed && !resumed.timed_out && resumed.frames == 512);
	available = 0;
	for (int i = 0; i < 23; ++i) { const auto r = tick(); assert(!r.started && !r.held); }

	// A stopped producer cannot leave recovery latched forever.
	controller.reset();
	available = 512;
	tick();
	tick();
	assert(tick().started);
	std::uint32_t held_frames = 0;
	for (int i = 0; i < 12; ++i)
	{
		const auto r = tick();
		if (r.held) held_frames += 512;
		if (r.resumed) { assert(r.timed_out); break; }
		assert(i < 11);
	}
	assert(held_frames > 0 && held_frames <= 4800);
	assert(!tick().held);

	// Disabled buffering never withholds PCM; reset also exits a pending refill.
	controller.configure(48000, 1632, 2592, false);
	for (int i = 0; i < 100; ++i)
	{
		available = 128;
		const auto r = tick();
		assert(r.frames == 128 && !r.held && !r.started);
	}
	controller.configure(48000, 1632, 2592, true);
	available = 128;
	tick();
	assert(tick().started);
	controller.reset();
	available = 512;
	assert(tick().frames == 512);

	// A negotiated request larger than the ring remains bounded as well.
	controller.configure(48000, 99999, 256, true);
	for (int i = 0; i < 30; ++i)
	{
		available = 256;
		const auto r = tick();
		assert(!r.held); // Reachable threshold is capacity, not an impossible request.
	}
}

void test_persistent_starvation()
{
	refill_controller controller;
	controller.configure(48000, 1632, 2592, true);
	std::uint32_t available = 0, starts = 0, timeouts = 0, held = 0;
	auto read = [&](std::uint32_t requested)
	{
		const auto count = std::min(available, requested);
		available -= count;
		return count;
	};
	// A producer supplying one quarter of device demand cannot refill in
	// 100 ms. After that failure, preserve its PCM rather than muting for
	// another attempt every 250 ms throughout a one-minute stall.
	for (unsigned i = 0; i < 5625; ++i)
	{
		available += 128;
		const auto r = controller.read(512, available, read);
		starts += r.started;
		timeouts += r.timed_out;
		if (r.held) held += 512;
	}
	assert(starts == 1 && timeouts == 1 && held <= 4800);
	// Sustained recovery rearms handling of a later, independent underrun.
	for (unsigned i = 0; i < 100; ++i)
	{
		available = 512;
		assert(controller.read(512, available, read).frames == 512);
	}
	available = 0;
	assert(!controller.read(512, available, read).started);
	assert(controller.read(512, available, read).started);
}

int main()
{
	test_refill();
	test_persistent_starvation();
	for (auto rate : {44100u, 48000u, 96000u})
	{
		test_fades<float>(rate);
		test_fades<std::int16_t>(rate);
	}
	std::puts("Audio refill bounds/cooldown/reset, and float/S16 fade tests passed");
}
