#include "Emu/Cell/GuestClock.h"
#include "Emu/Cell/timers.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <barrier>
#include <cstdio>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <string>
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
#define ensure(x, ...) check(x)

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
static bool emulated_thread = true;
struct cpu_thread
{
	static cpu_thread* get_current()
	{
		static cpu_thread cpu;
		return emulated_thread ? &cpu : nullptr;
	}
};
struct
{
	template <typename... Args> void notice(const char*, Args...) {}
} sys_time;
struct
{
	struct { u64 clocks_scale = 100; int sleep_timers_accuracy = 0; } core;
} g_cfg;
u64 get_system_time() { return host_us; }
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
	emulated_thread = true;
}

#include "GuestStallIntegrationTests.h"

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
	clock.pause([] { return u64{1000}; });
	clock.resume([] { return u64{26'001'000}; });
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
	// Blocking host work excludes time without a manual pause or parked PPU.
	// The same clock must drive LV2 waits, microseconds and SPU/PPU timebases.
	for (u64 frequency : {0u, 24'000'000u})
	{
		for (u64 scale : {50u, 100u, 200u})
		{
			for (bool signal_after_stall : {false, true})
			{
				reset();
				synthetic_tsc_frequency = frequency;
				g_cfg.core.clocks_scale = scale;
				initialize_timebased_time(0, false);
				ppu_thread waiter;
				waiter.end_time = 5'000'000;
				const u64 begin = host_us;
				u64 token = 0;
				u64 guest_at_stall = 0, tb_at_stall = 0;
				actions.push_back({begin + 100'000, [&]
				{
					token = begin_guest_time_stall();
					guest_at_stall = get_guest_system_time();
					tb_at_stall = get_timebased_time();
				}});
				actions.push_back({begin + 25'100'000, [&]
				{
					check(get_guest_system_time() == guest_at_stall);
					check(get_timebased_time() == tb_at_stall);
					check(convert_to_timebased_time(host_us) == tb_at_stall);
					check(waiter.state.bits == 0); // no emulator Pause occurred
					end_guest_time_stall(token, begin + 100'000, "synthetic shader compile");
				}});
				if (signal_after_stall)
					actions.push_back({begin + 25'200'000, [&] { waiter.state.bits |= cpu_flag::signal; }});
				check(lv2_obj::wait_timeout(5'000'000, &waiter, true, false) == !signal_after_stall);
				check(host_us == begin + (signal_after_stall ? 25'200'000 : 25'000'000 + 5'000'000 * 100 / scale));
			}
		}
	}

	// Independent scopes resume only after their union has ended, in either
	// order relative to an idempotent manual/background pause.
	for (bool pause_first : {false, true})
	{
		for (bool resume_first : {false, true})
		{
			reset();
			host_us += 100;
			if (pause_first) pause_guest_time();
			const u64 a = begin_guest_time_stall();
			host_us += 1'000'000;
			const u64 b = begin_guest_time_stall();
			if (!pause_first) pause_guest_time();
			pause_guest_time();
			host_us += 1'000'000;
			end_guest_time_stall(a, host_us, "first");
			if (resume_first) resume_guest_time();
			host_us += 1'000'000;
			check(get_guest_system_time() == 100);
			end_guest_time_stall(b, host_us, "second");
			if (!resume_first)
			{
				host_us += 1'000'000;
				check(get_guest_system_time() == 100);
				resume_guest_time();
			}
			resume_guest_time();
			host_us += 10;
			check(get_guest_system_time() == 110);
		}
	}

	// RAII handles exceptions; async workers and disabled scopes retain time.
	reset();
	try
	{
		const guest_time_stall hold("exception test");
		host_us += 30'000'000;
		check(get_guest_system_time() == 0);
		throw 1;
	}
	catch (int) {}
	host_us += 10;
	check(get_guest_system_time() == 10);
	for (bool background_worker : {false, true})
	{
		reset();
		emulated_thread = !background_worker;
		{
			const guest_time_stall hold("nonblocking", background_worker);
			host_us += 1'000'000;
			check(get_guest_system_time() == 1'000'000);
		}
	}

	// Stop/reset/new boot and save-state rebasing invalidate stale owners.
	reset();
	const u64 old = begin_guest_time_stall();
	initialize_timebased_time(800'000'000, false);
	const u64 fresh = begin_guest_time_stall();
	end_guest_time_stall(old, host_us, "old boot");
	host_us += 20'000'000;
	check(get_guest_system_time() == 10'000'000);
	end_guest_time_stall(fresh, host_us, "new boot");
	host_us += 100;
	check(get_guest_system_time() == 10'000'100);

	// An ordinary unsignalled guest deadline must still expire.
	reset();
	ppu_thread ordinary;
	ordinary.end_time = 5'000'000;
	const u64 wait_begin = host_us;
	check(lv2_obj::wait_timeout(5'000'000, &ordinary, true, false));
	check(host_us == wait_begin + 5'000'000);

	// Stop and forced notifications remain actionable during host work.
	for (u32 flag : {cpu_flag::exit, cpu_flag::notify})
	{
		reset();
		ppu_thread waiter;
		waiter.end_time = 5'000'000;
		const u64 held = begin_guest_time_stall();
		actions.push_back({host_us + 100'000, [&] { waiter.state.bits |= flag; }});
		check(lv2_obj::wait_timeout(5'000'000, &waiter, true, false) == (flag == cpu_flag::notify));
		end_guest_time_stall(held, host_us, "stopped work");
		host_us += 10;
		check(get_guest_system_time() == 10);
	}

	// Real concurrent writers: all hold at 1000, release together at 2000.
	// The last owner resumes once, regardless of interleaving.
	guest_clock concurrent;
	std::barrier gate(9);
	std::vector<std::thread> threads;
	for (unsigned i = 0; i < 8; ++i)
	{
		threads.emplace_back([&]
		{
			const u64 token = concurrent.hold([] { return u64{1000}; });
			gate.arrive_and_wait();
			gate.arrive_and_wait();
			concurrent.release(token, [] { return u64{2000}; });
		});
	}
	gate.arrive_and_wait();
	check(concurrent.get(2000) == 1000);
	gate.arrive_and_wait();
	for (auto& thread : threads) thread.join();
	check(concurrent.get(2100) == 1100);

	// No reader may pass a writer that has marked a transition but has not
	// sampled its time yet. This closes delayed-writer deadline underflow.
	guest_clock transition;
	std::promise<void> sampling, publish, reading;
	auto permission = publish.get_future();
	auto writer = std::async(std::launch::async, [&]
	{
		transition.pause([&]
		{
			sampling.set_value();
			permission.wait();
			return u64{1000};
		});
	});
	sampling.get_future().wait();
	auto reader = std::async(std::launch::async, [&]
	{
		reading.set_value();
		return transition.get(1500);
	});
	reading.get_future().wait();
	check(reader.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
	publish.set_value();
	writer.get();
	check(reader.get() == 1000);
	transition.resume([] { return u64{2000}; });
	check(transition.get(2100) == 1100);

	// A failing time source restores the snapshot and leaves no orphan hold.
	guest_clock failing;
	try { failing.hold([]() -> u64 { throw 1; }); }
	catch (int) {}
	check(failing.get(2000) == 2000);
	const u64 token = failing.hold([] { return u64{3000}; });
	failing.release(token, [] { return u64{5000}; });
	check(failing.get(5100) == 3100);
	stall_integration::run();
	std::printf("PASS guest clocks and host-stall-aware LV2 waits: %u checks\n", checks);
}
