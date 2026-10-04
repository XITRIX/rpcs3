#include "ios/IOSAudioSession.h"
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <iostream>
#include <thread>

struct FakeUnit
{
	bool running = false;
	bool fail_start = false;
	bool fail_stop = false;
	unsigned starts = 0;
	unsigned stops = 0;
};
using AudioUnit = FakeUnit*;
int AudioOutputUnitStart(AudioUnit unit)
{
	++unit->starts;
	if (unit->fail_start) return -1;
	unit->running = true;
	return 0;
}
int AudioOutputUnitStop(AudioUnit unit)
{
	++unit->stops;
	if (unit->fail_stop) return -1;
	unit->running = false;
	return 0;
}
int AudioUnitUninitialize(AudioUnit) { return 0; }
int AudioComponentInstanceDispose(AudioUnit) { return 0; }
bool check_status(int status, const char*) { return status == 0; }
struct
{
	template <typename... T> void error(T...) {}
	template <typename... T> void notice(T...) {}
} IOSAudio;

class IOSAudioBackend
{
public:
	std::mutex m_control_mutex;
	std::mutex m_cb_mutex;
	AudioUnit m_unit = nullptr;
	bool m_playing = false;
	bool m_session_active = true;
	std::atomic_bool m_operational = true;
	std::atomic_bool m_needs_fade_reset = false;
	unsigned m_bytes_per_frame = 8;
	unsigned errors = 0;
	struct { void reset_timing() {} } m_diagnostics;
	struct { void reset(unsigned) {} } m_fader;
	unsigned get_sampling_rate() { return 48000; }
	void notify_error() { ++errors; }
	void log_diagnostics(bool) {}
	bool IsPlaying();
	void Play();
	void Pause();
	void close_unlocked();
	void Close();
	void set_session_active(bool active);
};

// Production method definitions are inserted by run-audio-session-tests.py.
/* PRODUCTION_METHODS */

int main()
{
	rpcs3::ios::audio_session_lifecycle session;
	FakeUnit unit;
	IOSAudioBackend backend;
	backend.m_unit = &unit;
	const auto callback = +[](void* context, bool active)
	{
		static_cast<IOSAudioBackend*>(context)->set_session_active(active);
	};
	session.attach(&backend, callback);
	backend.Play();
	assert(backend.IsPlaying() && !unit.running && unit.starts == 0);
	session.set_active(true);
	assert(unit.running && unit.starts == 1);

	// XMB's direct provider never calls Pause/Play during these scene changes.
	for (unsigned i = 0; i < 3; ++i)
	{
		session.set_active(false);
		assert(!unit.running && backend.IsPlaying());
		session.set_active(false);
		assert(unit.stops == i + 1);
		session.set_active(true);
		session.set_active(true);
		assert(unit.running && unit.starts == i + 2);
	}

	// cellAudio may request Pause after the scene has already suspended I/O.
	session.set_active(false);
	backend.Pause();
	const auto starts = unit.starts;
	session.set_active(true);
	assert(!unit.running && !backend.IsPlaying() && unit.starts == starts);
	backend.Play();
	assert(unit.running && backend.IsPlaying());
	backend.Pause();
	session.set_active(false);
	session.set_active(true);
	assert(!unit.running && !backend.IsPlaying());

	// Failure reaches the providers' existing backend-recovery path.
	backend.Play();
	session.set_active(false);
	unit.fail_start = true;
	session.set_active(true);
	assert(!backend.m_operational && backend.errors == 1 && !unit.running);
	unit.fail_start = false;
	backend.m_operational = true;
	backend.Pause();
	backend.Play();
	unit.fail_stop = true;
	session.set_active(false);
	assert(!backend.m_operational && backend.errors == 2);
	unit.fail_stop = false;
	backend.Close();
	session.set_active(true);
	assert(!backend.m_unit && !backend.IsPlaying() && !unit.running);
	session.detach(&backend);

	// A newly created backend inherits the current session state; detached
	// instances never receive another callback (same-process Stop/second boot).
	FakeUnit second_unit;
	IOSAudioBackend second;
	second.m_unit = &second_unit;
	session.attach(&second, callback);
	second.Play();
	assert(second_unit.running);
	session.detach(&second);
	second.Close();
	session.set_active(false);
	session.set_active(true);
	assert(!second_unit.running);

	// Destruction waits for an in-flight notification. The handler deliberately
	// blocks so this does not rely on timing/sleeps to create the race.
	struct Context
	{
		std::mutex mutex;
		std::condition_variable cv;
		bool entered = false;
		bool release = false;
		unsigned calls = 0;
	} context;
	session.attach(&context, [](void* ptr, bool active)
	{
		auto& value = *static_cast<Context*>(ptr);
		std::unique_lock lock{value.mutex};
		++value.calls;
		if (active) return;
		value.entered = true;
		value.cv.notify_all();
		value.cv.wait(lock, [&] { return value.release; });
	});
	std::thread delivery([&] { session.set_active(false); });
	{
		std::unique_lock lock{context.mutex};
		context.cv.wait(lock, [&] { return context.entered; });
	}
	std::atomic_bool detached = false;
	std::thread destruction([&] { session.detach(&context); detached = true; });
	assert(!detached);
	{
		std::lock_guard lock{context.mutex};
		context.release = true;
	}
	context.cv.notify_all();
	delivery.join();
	destruction.join();
	assert(detached && context.calls == 2);
	session.set_active(true);
	assert(context.calls == 2);
	std::cout << "RemoteIO session suspend/resume, playback intent, errors and observer lifetime passed\n";
}
