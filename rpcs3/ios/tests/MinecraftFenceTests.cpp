#include <algorithm>
#include <atomic>
#include <barrier>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
#include <ctime>
using u32 = unsigned;
#ifdef MINECRAFT_NATIVE_ATOMIC
#include "util/atomic.hpp"
using atomic_adapter = atomic_t<u32>;
#else
struct atomic_adapter
{
	std::atomic<u32> value{0};
	operator u32() const
	{
		return value.load();
	}
	void release(u32 x)
	{
		value.store(x, std::memory_order_release);
	}
	void wait(u32 x) const
	{
		value.wait(x);
	}
	void notify_all()
	{
		value.notify_all();
	}
};
#endif
namespace utils
{
	void pause()
	{
		asm volatile("yield" ::: "memory");
	}
} // namespace utils
#define FIELDS              \
	atomic_adapter flushed{0}; \
	void wait_flush();      \
	void signal_flushed();
namespace old
{
	struct fence
	{
#ifdef MINECRAFT_NATIVE_ATOMIC
		atomic_t<bool> flushed = false;
		void wait_flush();
		void signal_flushed();
#else
		FIELDS
#endif
	};
#include "old-wait_flush.inc"
#include "old-signal_flushed.inc"
} // namespace old
namespace candidate
{
	struct fence
	{
		FIELDS
	};
#include "new-wait_flush.inc"
#include "new-signal_flushed.inc"
} // namespace candidate
template <class F>
void verify()
{
	F f;
	unsigned payload = 0;
	std::barrier phase(2);
	std::thread producer([&]
		{
			for (unsigned i = 1; i <= 10000; ++i)
			{
				phase.arrive_and_wait();
				payload = i;
				f.signal_flushed();
				phase.arrive_and_wait();
			}
		});
	for (unsigned i = 1; i <= 10000; ++i)
	{
		f.flushed.release(0);
		phase.arrive_and_wait();
		if (i % 3 == 0)
			std::this_thread::yield();
		f.wait_flush();
		assert(payload == i && f.flushed == 1);
		phase.arrive_and_wait();
	}
	producer.join();
	f.flushed.release(0);
	std::atomic<unsigned> ready = 0;
	std::vector<std::thread> waiters;
	for (unsigned i = 0; i < 4; ++i)
		waiters.emplace_back([&]
			{
				++ready;
				f.wait_flush();
				assert(payload == 12345);
			});
	while (ready != 4)
		std::this_thread::yield();
	std::this_thread::sleep_for(std::chrono::milliseconds(5));
	payload = 12345;
	f.signal_flushed();
	for (auto& t : waiters)
		t.join();
	f.wait_flush();
}
double cpu_ns()
{
	timespec t{};
	assert(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) == 0);
	return double(t.tv_sec) * 1e9 + t.tv_nsec;
}
struct timing
{
	double cpu, wall, lag;
};
template <class F>
timing timed(unsigned us)
{
	F f;
	std::atomic<bool> ready = false;
	std::chrono::steady_clock::time_point signalled;
	std::thread producer([&]
		{
			while (!ready.load())
				std::this_thread::yield();
			std::this_thread::sleep_for(std::chrono::microseconds(us));
			signalled = std::chrono::steady_clock::now();
			f.signal_flushed();
		});
	auto t = std::chrono::steady_clock::now();
	double cpu = cpu_ns();
	ready = true;
	f.wait_flush();
	cpu = cpu_ns() - cpu;
	auto end = std::chrono::steady_clock::now();
	producer.join();
	assert(end >= signalled);
	return {cpu, std::chrono::duration<double, std::micro>(end - t).count(), std::chrono::duration<double, std::micro>(end - signalled).count()};
}
int main(int argc, char**)
{
	verify<old::fence>();
	verify<candidate::fence>();
	std::puts("PASS: extracted fence wait/signal; 10000 release/acquire payloads, reset cycles, signal-before-wait, delayed signal and four waiters (backend selected by --native-atomic; otherwise std atomic)");
	if (argc == 1)
		return 0;
	for (unsigned us : {50, 1000, 20000})
	{
		std::vector<double> cpu[2], lag[2], wall[2];
		for (unsigned r = 0; r < 22; ++r)
			for (unsigned j = 0; j < 2; ++j)
			{
				unsigned v = (r + j) % 2;
				auto n = v ? timed<candidate::fence>(us) : timed<old::fence>(us);
				if (r >= 2)
				{
					cpu[v].push_back(n.cpu);
					lag[v].push_back(n.lag);
					wall[v].push_back(n.wall);
				}
			}
		for (unsigned v = 0; v < 2; ++v)
		{
			std::sort(cpu[v].begin(), cpu[v].end());
			std::sort(lag[v].begin(), lag[v].end());
			std::sort(wall[v].begin(), wall[v].end());
		}
		std::printf("fence_%uus CPU baseline=%.1f candidate=%.1f us saving=%.2f%% wake median/p95 baseline=%.2f/%.2f candidate=%.2f/%.2f us wall baseline=%.2f candidate=%.2f us\n", us, cpu[0][10] / 1000, cpu[1][10] / 1000, 100 * (1 - cpu[1][10] / cpu[0][10]), lag[0][10], lag[0][18], lag[1][10], lag[1][18], wall[0][10], wall[1][10]);
	}
}
