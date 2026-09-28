#include "Emu/Cell/GuestClock.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <functional>
#include <thread>
#include <vector>
#include <time.h>

static u64 host_us = 1'000'000'000;
static unsigned checks = 0;
static u64 synthetic_tsc_frequency = 0;
static void check(bool value)
{
	if (!value)
	{
		std::fprintf(stderr, "FAIL guest clock check %u at host %llu\n", checks + 1, static_cast<unsigned long long>(host_us));
		std::abort();
	}
	checks++;
}
#define ensure(x) check(x)

namespace utils
{
	u64 get_tsc_freq() { return synthetic_tsc_frequency; }
	u64 get_tsc() { return host_us * (synthetic_tsc_frequency / 1'000'000); }
}

// Only the clock source and unrelated kernel/thread dependencies are stubbed.
// Production time conversion, initialization and wait bodies execute unchanged.
static int test_clock_gettime(clockid_t, timespec* ts)
{
	ts->tv_sec = host_us / 1'000'000;
	ts->tv_nsec = (host_us % 1'000'000) * 1000;
	return 0;
}
#define clock_gettime test_clock_gettime
static constexpr u64 g_timebase_freq = 80'000'000;
static u64 g_timebase_offs = 0;
static u64 systemtime_offset = 0;
static guest_clock s_guest_clock;
struct
{
	struct { u64 clocks_scale = 100; int sleep_timers_accuracy = 0; } core;
} g_cfg;
u64 get_system_time() { return host_us; }
u64 get_active_system_time(u64 time = umax);
u64 get_guest_system_time(u64 time = umax);
#include "GuestTime.inc"
#undef clock_gettime

struct cpu_flag { static constexpr u32 notify = 1, signal = 2, exit = 4, dbg_global_pause = 8; };
template <typename T> struct atomic_bs_t
{
	u32 bits = 0;
	operator u32() const { return bits; }
};
struct ppu_thread { u64 end_time = umax; atomic_bs_t<cpu_flag> state; };
enum class thread_state { running, aborting };
enum sleep_timers_accuracy_level { _usleep = 1, _all_timers = 2 };
static bool aborting = false;
static bool is_stopped(u32 state) { return state & cpu_flag::exit; }
struct action { u64 at; std::function<void()> run; };
static std::vector<action> actions;
static unsigned waits = 0;
namespace thread_ctrl
{
	thread_state state() { return aborting ? thread_state::aborting : thread_state::running; }
	void wait_on(const atomic_bs_t<cpu_flag>&, u32, u64 timeout)
	{
		check(++waits < 1000);
		const u64 until = host_us + timeout;
		if (!actions.empty() && actions.front().at <= until)
		{
			auto next = std::move(actions.front());
			actions.erase(actions.begin());
			host_us = next.at;
			next.run();
		}
		else host_us = until;
	}
}
struct lv2_obj
{
	static constexpr u64 max_timeout = u64{umax} / 1000;
	static bool wait_timeout(u64, ppu_thread* = nullptr, bool = true, bool = false);
};
#include "GuestWait.inc"

struct cell_audio_thread
{
	std::array<u64, 8> guest_timestamps;
	u64 get_port_guest_timestamp(u32 port_number);
};
#include "AudioTimestampRestore.inc"

static void reset(u64 saved = 0)
{
	host_us = 1'000'000'000;
	g_cfg.core.clocks_scale = 100;
	initialize_timebased_time(saved, false);
	actions.clear();
	waits = 0;
	aborting = false;
}

int main()
{
	for (u64 frequency : {0u, 24'000'000u})
	{
		synthetic_tsc_frequency = frequency;
		for (u64 scale : {50u, 100u, 200u})
		{
			reset();
			g_cfg.core.clocks_scale = scale;
			initialize_timebased_time(0, false);
			host_us += 100'000;
			const u64 before = get_guest_system_time();
			const u64 tb = get_timebased_time();
			check(before == 100'000 * scale / 100);
			check(tb == before * 80);
			pause_guest_time();
			host_us += 25'000'000;
			check(get_guest_system_time() == before);
			check(get_timebased_time() == tb);
			check(convert_to_timebased_time(host_us) == tb);
			pause_guest_time(); // paused -> frozen must not restart accounting
			resume_guest_time();
			check(get_guest_system_time() == before);
			resume_guest_time(); // duplicate resume
			host_us += 100'000;
			check(get_guest_system_time() == before * 2);
			check(get_timebased_time() == tb * 2);
		}
	}

	// A timeout already pending when a 25-second shader pause begins must
	// still allow its producer to signal after Resume (GT6 has a 5-second wait).
	for (u64 scale : {50u, 100u, 200u})
	{
		for (bool signal_after_resume : {false, true})
		{
			reset();
			g_cfg.core.clocks_scale = scale;
			initialize_timebased_time(0, false);
			ppu_thread cpu;
			cpu.end_time = get_guest_system_time() + 5'000'000;
			const u64 begin = host_us;
			actions.push_back({begin + 100'000, [&] { pause_guest_time(); cpu.state.bits |= cpu_flag::dbg_global_pause; }});
			actions.push_back({begin + 25'100'000, [&] { resume_guest_time(); cpu.state.bits &= ~cpu_flag::dbg_global_pause; }});
			if (signal_after_resume)
				actions.push_back({begin + 25'200'000, [&] { cpu.state.bits |= cpu_flag::signal; }});
			check(lv2_obj::wait_timeout(5'000'000, &cpu, true, false) == !signal_after_resume);
			check(host_us == begin + (signal_after_resume ? 25'200'000 : 25'000'000 + 5'000'000 * 100 / scale));
		}
	}

	reset();
	ppu_thread cpu;
	cpu.end_time = 5'000'000;
	actions.push_back({host_us + 100'000, [&] { pause_guest_time(); cpu.state.bits |= cpu_flag::dbg_global_pause; }});
	actions.push_back({host_us + 200'000, [&] { cpu.state.bits |= cpu_flag::exit; }});
	check(!lv2_obj::wait_timeout(5'000'000, &cpu, true, false)); // Stop while paused

	reset();
	cpu = {};
	cpu.state.bits = cpu_flag::notify;
	check(lv2_obj::wait_timeout(5'000'000, &cpu, true, false)); // forced real deadline
	cpu.state.bits = cpu_flag::signal;
	check(!lv2_obj::wait_timeout(5'000'000, &cpu, true, false));
	cpu.state.bits = 0;
	cpu.end_time = get_guest_system_time();
	check(lv2_obj::wait_timeout(5'000'000, &cpu, true, false)); // already elapsed

	// Save-state rebasing and a subsequent cold boot reset previous pauses.
	reset(800'000'000);
	check(get_timebased_time() == 800'000'000);
	check(get_guest_system_time() == 10'000'000);
	for (unsigned i = 0; i < 100; ++i)
	{
		host_us += 100;
		pause_guest_time();
		host_us += 25'000'000;
		resume_guest_time();
		check(get_timebased_time() == 800'000'000 + (i + 1) * 8000);
	}
	initialize_timebased_time(0, false);
	check(get_guest_system_time() == 0);
	check(get_timebased_time() == 0);
	initialize_timebased_time(0, true);
	check(get_active_system_time() == host_us);

	// Keep the timebase's sub-microsecond precision while running.
	guest_clock clock;
	clock.pause(1000);
	clock.resume(26'001'000);
	check(clock.get(26'001'001 * 80ull + 17, 80) == 1001 * 80 + 17);
	check(clock.get(26'000'999) == 1000); // timestamp captured before Resume

	// Audio must retain a block's guest timestamp across later pauses. The
	// production assignment/conversion are extracted by the runner.
	reset();
	host_us += 10'000'000;
	struct { u32 number = 0; } port;
	const u32 portNum = port.number;
	cell_audio_thread g_audio;
	g_audio.guest_timestamps.fill(umax);
	auto& guest_timestamps = g_audio.guest_timestamps;
	const u64 timestamp = get_system_time();
#include "AudioTimestampCapture.inc"
	pause_guest_time();
	host_us += 25'000'000;
	resume_guest_time();
	u64 result = 0;
	u64* stamp = &result;
	const u64 delta_tag_stamp = 1'000'000;
#include "AudioTimestampQuery.inc"
	check(result == 9'000'000);
	// Runtime anchors are absent after loading either old or new save states.
	// Rebase lazily after guest time restoration, and keep repeated queries stable.
	g_audio.guest_timestamps.fill(umax);
	initialize_timebased_time(800'000'000, false);
	check(g_audio.get_port_guest_timestamp(portNum) == 10'000'000);
	host_us += 1'000'000;
	check(g_audio.get_port_guest_timestamp(portNum) == 10'000'000);
	std::printf("PASS guest clocks and pause-aware LV2 waits: %u checks\n", checks);
}
