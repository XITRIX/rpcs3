// Exercises production channel operations, wait registration, notification and
// LLVM helper. Only the owning CPU/thread's lifecycle fields are fixture data.
#include "util/atomic.hpp"
#include "util/asm.hpp"
#include "util/endian.hpp"
#include "util/bless.hpp"
#include "util/shared_ptr.hpp"
#include "Utilities/StrFmt.h"
#include "Utilities/bit_set.h"
#include "util/logs.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <future>
#include <random>
#include <ctime>
#include <algorithm>
#include "SPUMailboxTestSupport.h"

struct fixture_thread
{
	atomic_t<u32> m_sync{0};
	stx::atomic_ptr<int> m_taskq{};
};
thread_local fixture_thread* g_tls_this_thread = nullptr;
struct thread_ctrl
{
#include "SPUMailboxWait.inc"
};
#include "SPUMailboxCPUFlags.inc"
struct cpu_thread
{
	atomic_bs_t<cpu_flag> state{};
	u32 checks = 0;
	bool is_stopped() const
	{
		return ::is_stopped(state);
	}
	// Lifecycle adapter models the caller contract, not full CPUThread.cpp.
	// Physical pause/stop and savestates still require an emulator run.
	bool check_state()
	{
		checks++;
		while (true)
		{
			const auto old = +state;
			if (!(old & cpu_flag::pause) || is_stopped())
				break;
			state.wait(old);
		}
		if (!is_stopped())
			state -= cpu_flag::wait;
		return is_stopped();
	}
};
using spu_thread = cpu_thread;
struct fixture_escape
{
};
namespace spu_runtime
{
	[[noreturn]] void g_escape(spu_thread*)
	{
		throw fixture_escape{};
	}
} // namespace spu_runtime
#include "SPUMailboxChannel.inc"
#include "SPUMailboxHelper.inc"
static u32 observed_wait(spu_thread* cpu, spu_channel_4_t* ch)
{
	try
	{
		return wait_inbox(cpu, ch);
	}
	catch (const fixture_escape&)
	{
		return umax;
	}
}
using namespace std::chrono_literals;
static unsigned cases = 0;
static void require(bool value, const char* reason)
{
	if (!value)
	{
		std::cerr << "Mailbox failure: " << reason << '\n';
		std::abort();
	}
}
static void interrupt(fixture_thread& thread)
{
	thread.m_sync.bit_test_set(2);
	thread.m_sync.notify_all();
}
static double ns(clockid_t clock)
{
	timespec t{};
	require(clock_gettime(clock, &t) == 0, "clock_gettime");
	return double(t.tv_sec) * 1e9 + t.tv_nsec;
}
static u32 poll_count(const spu_channel_4_t& ch)
{
	// Same acquire load/count extraction as the unoptimized LLVM RCHCNT.
	return (__atomic_load_n(reinterpret_cast<const u32*>(&ch.values.raw()), __ATOMIC_ACQUIRE) >> 8) & 7;
}
static void ready_and_liveouts()
{
	std::mt19937 random(0x4a4);
	for (u32 count = 1; count <= 4; count++)
		for (int immediate : {0, 1})
			for (unsigned alias = 0; alias < 2; alias++)
				for (unsigned repeat = 0; repeat < 256; repeat++)
				{
					spu_channel_4_t ch{};
					cpu_thread cpu;
					for (u32 i = 0; i < count; i++)
						require(ch.push(0xabcd0000 + i).op_done, "prefill");
					const auto before = ch.values.load();
					const auto observed = wait_inbox(&cpu, &ch);
					require(observed == count, "preserve count 1..4");
					require(cpu.checks == 0 && !cpu.state, "ready path does not park or alter CPU state");
					std::array<std::array<u32, 4>, 128> expected{}, actual{};
					for (auto& reg : expected)
						for (auto& lane : reg)
							lane = random();
					actual = expected;
					const auto cmp = alias ? 3 : 2;
					// Independent scalar SPU oracle; all 128 register live-outs.
					expected[3] = {0, 0, 0, count};
					const auto input = expected[3];
					for (unsigned lane = 0; lane < 4; lane++)
						expected[cmp][lane] = input[lane] == static_cast<u32>(immediate) ? ~0u : 0;
					actual[3] = {0, 0, 0, observed};
					const auto value = actual[3];
					for (unsigned lane = 0; lane < 4; lane++)
						actual[cmp][lane] = value[lane] == static_cast<u32>(immediate) ? ~0u : 0;
					require(actual == expected, "register live-outs");
					require(std::bit_cast<u128>(ch.values.load()) == std::bit_cast<u128>(before), "wait does not consume/reorder ready data");
					for (u32 i = 0; i < count; i++)
					{
						u32 v{};
						require(ch.try_pop(v) && v == 0xabcd0000 + i, "message order after wait");
					}
					cases++;
				}
}
static void race_cases()
{
	for (unsigned i = 0; i < 1024; i++)
	{
		spu_channel_4_t ch{};
		cpu_thread cpu;
		fixture_thread ctl;
		std::promise<u32> promise;
		auto result = promise.get_future();
		std::thread worker([&]
			{
				g_tls_this_thread = &ctl;
				promise.set_value(observed_wait(&cpu, &ch));
			});
		// Publication races initial read, 128-bit registration, short spin and
		// the kernel wait. Also exercise deferred notification and stop alerts.
		if (i % 4)
			std::this_thread::sleep_for(std::chrono::microseconds((i % 17) * 3));
		if (i % 8 == 0)
			interrupt(ctl); // lifecycle alert with no stop/data must not finish the wait
		const bool stop = i % 5 == 0;
		const bool publish = !stop || i % 10 == 0;
		if (stop)
		{
			cpu.state += cpu_flag::stop;
			interrupt(ctl);
		}
		if (publish)
		{
			const bool postpone = i % 3 == 0;
			require(ch.push(i, postpone).op_done, "racing publish");
			if (postpone)
			{
				std::this_thread::yield();
				ch.notify();
			}
		}
		require(result.wait_for(2s) == std::future_status::ready, "no lost wake on publish/stop");
		const auto observed = result.get();
		worker.join();
		require(observed <= 1 || (stop && observed == umax), "bounded race result or stop escape");
		if (stop && !publish)
			require(observed == umax, "stopped empty wait escapes before guest continues");
		if (!stop)
		{
			require(observed == 1, "published count");
			require(!(cpu.state & cpu_flag::wait), "CPU wait state restored after publication");
		}
		u32 value{};
		require((ch.try_pop(value) != 0) == publish, "non-consuming stop/publish race");
		if (publish)
			require(value == i, "published payload unchanged");
		require(ch.values.load().waiting == 0, "waiting bit cleared after completion");
		cases++;
	}
	// A pure pause-style alert wakes the wait set but must neither consume data
	// nor invent a ready count. A subsequent producer still wakes the same waiter.
	spu_channel_4_t ch{};
	cpu_thread cpu;
	fixture_thread ctl;
	std::promise<u32> promise;
	auto result = promise.get_future();
	std::thread worker([&]
		{
			g_tls_this_thread = &ctl;
			promise.set_value(wait_inbox(&cpu, &ch));
		});
	std::this_thread::sleep_for(2ms);
	interrupt(ctl);
	require(result.wait_for(2ms) == std::future_status::timeout, "alert without data remains waiting");
	require(ch.push(0x12345678).op_done, "publish after alert");
	require(result.wait_for(2s) == std::future_status::ready && result.get() == 1, "resume producer wakes");
	worker.join();
	u32 value{};
	require(ch.try_pop(value) && value == 0x12345678, "alert preserves message");
	cases++;
}
static void suspend_handshake()
{
	spu_channel_4_t ch{};
	cpu_thread cpu;
	fixture_thread ctl;
	std::promise<u32> promise;
	auto result = promise.get_future();
	std::thread worker([&]
		{
			g_tls_this_thread = &ctl;
			promise.set_value(wait_inbox(&cpu, &ch));
		});
	const auto deadline = std::chrono::steady_clock::now() + 2s;
	while (!(cpu.state & cpu_flag::wait) && std::chrono::steady_clock::now() < deadline)
		std::this_thread::yield();
	require(!!(cpu.state & cpu_flag::wait), "parked inbox acknowledges global CPU suspend handshake");
	cpu.state += cpu_flag::pause;
	require(ch.push(42).op_done, "publish while suspended");
	require(result.wait_for(2ms) == std::future_status::timeout, "helper checks CPU state before guest continues");
	cpu.state -= cpu_flag::pause;
	cpu.state.notify_all();
	require(result.wait_for(2s) == std::future_status::ready && result.get() == 1, "resume completes non-consuming count wait");
	worker.join();
	require(cpu.checks == 1 && !cpu.state, "one post-wait state check restores running state");
	u32 value{};
	require(ch.try_pop(value) && value == 42, "suspend preserves mailbox payload");
	cases++;
}
struct timing
{
	double cpu_ns{}, wall_ns{}, wake_ns{};
};
static timing benchmark(bool candidate, unsigned rounds, unsigned delay_us)
{
	spu_channel_4_t ch{};
	cpu_thread cpu;
	fixture_thread ctl;
	std::atomic<unsigned> request{0};
	std::atomic<double> published{0};
	timing t;
	std::thread producer([&]
		{
			for (unsigned i = 1; i <= rounds; i++)
			{
				unsigned observed;
				while ((observed = request.load(std::memory_order_acquire)) < i)
					request.wait(observed);
				std::this_thread::sleep_for(std::chrono::microseconds(delay_us));
				published.store(ns(CLOCK_MONOTONIC_RAW), std::memory_order_release);
				require(ch.push(i).op_done, "benchmark publish");
			}
		});
	g_tls_this_thread = &ctl;
	const auto wall0 = ns(CLOCK_MONOTONIC_RAW), cpu0 = ns(CLOCK_THREAD_CPUTIME_ID);
	for (unsigned i = 1; i <= rounds; i++)
	{
		request.store(i, std::memory_order_release);
		request.notify_one();
		u32 count;
		do
		{
			count = candidate ? wait_inbox(&cpu, &ch) : poll_count(ch);
		} while (count != 1);
		t.wake_ns += ns(CLOCK_MONOTONIC_RAW) - published.load(std::memory_order_acquire);
		u32 value{};
		require(ch.try_pop(value) && value == i, "benchmark results");
	}
	t.cpu_ns = (ns(CLOCK_THREAD_CPUTIME_ID) - cpu0) / rounds;
	t.wall_ns = (ns(CLOCK_MONOTONIC_RAW) - wall0) / rounds;
	t.wake_ns /= rounds;
	producer.join();
	g_tls_this_thread = nullptr;
	return t;
}
int main(int argc, char**)
{
	utils::init_arm_timer_scale();
	ready_and_liveouts();
	race_cases();
	suspend_handshake();
	std::cout << "Production mailbox + native atomic wait: " << cases << " state/race cases passed\n";
	if (argc > 1)
	{
		for (unsigned delay : {50u, 500u, 2000u})
			for (unsigned round = 0; round < 8; round++)
			{
				timing baseline, candidate;
				if (round & 1)
				{
					candidate = benchmark(true, 128, delay);
					baseline = benchmark(false, 128, delay);
				}
				else
				{
					baseline = benchmark(false, 128, delay);
					candidate = benchmark(true, 128, delay);
				}
				std::cout << "bench delay_us=" << delay << " round=" << round << " baseline_cpu_ns=" << baseline.cpu_ns << " candidate_cpu_ns=" << candidate.cpu_ns
						  << " baseline_wall_ns=" << baseline.wall_ns << " candidate_wall_ns=" << candidate.wall_ns << " baseline_wake_ns=" << baseline.wake_ns << " candidate_wake_ns=" << candidate.wake_ns << '\n';
			}
	}
}
