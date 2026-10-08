#include "util/types.hpp"
#include <stdexcept>
#define ensure(x, ...)                          \
	do                                          \
	{                                           \
		if (!(x))                               \
			throw std::runtime_error("ensure"); \
	} while (false)
#include "Utilities/lockless.h"
#include "Emu/RSX/gcm_enums.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>
#include <atomic>
#include <thread>
namespace fmt
{
	[[noreturn]] void throw_exception(const char* x)
	{
		throw std::runtime_error(x);
	}
} // namespace fmt
// No sleeping consumer in this fixture: retain the actual queue/atomic/packet
// implementation, replacing only the wait-engine notification with a counter.
static unsigned notifications = 0;
void atomic_wait_engine::notify_one(const void*)
{
	++notifications;
}
void iota32(u32* dst, unsigned n)
{
	for (unsigned i = 0; i < n; ++i)
		dst[i] = i;
}
#include "index-generator.inc"
__attribute__((noinline)) void write_index_array_for_non_indexed_non_native_primitive_to_buffer(char* d, rsx::primitive_type p, unsigned n)
{
	write_non_native_indices_precomputed(d, p, n);
}
struct configuration
{
	struct
	{
		bool multithreaded_rsx = true;
	} video;
} g_cfg;
struct worker
{
	enum op
	{
		raw_copy,
		vector_copy,
		index_emulate,
		callback
	};
#include "transport.inc"
	lf_queue<transport_packet> m_work_queue;
	atomic_t<u64> m_enqueued_count = 0, m_processed_count = 0;
	void drain()
	{
		for (auto& j : m_work_queue.pop_all())
		{
			if (j.type == index_emulate)
				write_index_array_for_non_indexed_non_native_primitive_to_buffer(static_cast<char*>(j.dst), rsx::primitive_type(j.aux_param0), j.length);
			else if (j.type == raw_copy)
				std::memcpy(j.dst, j.src, j.length);
			else if (j.type == vector_copy)
				std::memcpy(j.dst, j.opt_storage.data(), j.length);
			++m_processed_count;
		}
	}
};
namespace old
{
	struct dma_manager
	{
		std::shared_ptr<worker> m_thread = std::make_shared<worker>();
		void emulate_as_indexed(void*, rsx::primitive_type, u32);
	};
#include "old-dispatch.inc"
} // namespace old
namespace candidate
{
	struct dma_manager
	{
		std::shared_ptr<worker> m_thread = std::make_shared<worker>();
		void emulate_as_indexed(void*, rsx::primitive_type, u32);
	};
#include "new-dispatch.inc"
} // namespace candidate
static volatile u64 sink;
int main(int argc, char**)
{
	old::dma_manager o;
	candidate::dma_manager n;
	alignas(16) std::array<u32, 200000> a{}, b{}, oracle{};
	std::mt19937 rng(310);
	for (unsigned i = 0; i < 10000; ++i)
	{
		unsigned count = rng() % 3000, offset = rng() % 128;
		auto mode = i % 3 == 0 ? rsx::primitive_type::triangle_fan : rsx::primitive_type::quads;
		if (mode == rsx::primitive_type::triangle_fan)
			count = std::max(2u, count);
		if (i % 5 == 0)
		{
			std::fill_n(oracle.data(), 1024, i);
			for (auto* thr : {o.m_thread.get(), n.m_thread.get()})
			{
				++thr->m_enqueued_count;
				thr->m_work_queue.push(thr == o.m_thread.get() ? a.data() : b.data(), oracle.data(), 1024 * sizeof(u32));
			}
		}
		o.emulate_as_indexed(a.data() + offset, mode, count);
		n.emulate_as_indexed(b.data() + offset, mode, count);
		if (i % 4 == 0)
		{
			o.m_thread->drain();
			n.m_thread->drain();
			assert(a == b);
		}
	}
	o.m_thread->drain();
	n.m_thread->drain();
	assert(a == b);
	// The completion counter is the acquire/release boundary that makes a
	// direct upload safe while a real consumer drains earlier queue packets.
	g_cfg.video.multithreaded_rsx = true;
	std::atomic<bool> complete = false;
	auto consume = [&](worker& thread)
	{
		while (!complete.load() || thread.m_processed_count.load() < thread.m_enqueued_count.load())
		{
			thread.drain();
			std::this_thread::yield();
		}
	};
	std::thread original_consumer([&] { consume(*o.m_thread); });
	std::thread candidate_consumer([&] { consume(*n.m_thread); });
	for (unsigned i = 0; i < 2000; ++i)
	{
		const unsigned count = rng() % 1500, offset = rng() % 128;
		o.emulate_as_indexed(a.data() + offset, rsx::primitive_type::quads, count);
		n.emulate_as_indexed(b.data() + offset, rsx::primitive_type::quads, count);
	}
	complete = true;
	original_consumer.join();
	candidate_consumer.join();
	assert(a == b);
	for (bool mt : {false, true})
		for (unsigned count : {0u, 1u, 2u, 3u, 4u, 511u, 512u, 513u, 65535u, 65536u, 65537u})
		{
			g_cfg.video.multithreaded_rsx = mt;
			a.fill(~0u);
			b = a;
			oracle = a;
			o.emulate_as_indexed(a.data(), rsx::primitive_type::quads, count);
			n.emulate_as_indexed(b.data(), rsx::primitive_type::quads, count);
			o.m_thread->drain();
			n.m_thread->drain();
			for (unsigned i = 0; i < count / 4; ++i)
			{
				oracle[i * 6] = i * 4;
				oracle[i * 6 + 1] = i * 4 + 1;
				oracle[i * 6 + 2] = oracle[i * 6 + 3] = i * 4 + 2;
				oracle[i * 6 + 4] = i * 4 + 3;
				oracle[i * 6 + 5] = i * 4;
			}
			assert(a == b && a == oracle);
		}
	std::puts("PASS: extracted index dispatch; native packets/lf_queue/atomics; 10k aliasing copy/index jobs, 2000 concurrent-consumer jobs and threshold/tail/fallback boundaries");
	if (argc == 1)
		return 0;
	g_cfg.video.multithreaded_rsx = true;
	for (unsigned count : {4, 64, 256, 512, 513, 4096})
	{
		std::vector<double> times[2];
		for (unsigned r = 0; r < 12; ++r)
			for (unsigned j = 0; j < 2; ++j)
			{
				unsigned v = (r + j) % 2;
				auto t = std::chrono::steady_clock::now();
				u64 s = 0;
				for (unsigned i = 0; i < 100000; ++i)
				{
					asm volatile("" ::: "memory");
					if (v)
					{
						n.emulate_as_indexed(b.data(), rsx::primitive_type::quads, count);
						n.m_thread->drain();
						s += b[0];
					}
					else
					{
						o.emulate_as_indexed(a.data(), rsx::primitive_type::quads, count);
						o.m_thread->drain();
						s += a[0];
					}
				}
				sink = s;
				double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t).count() / 100000;
				if (r >= 2)
					times[v].push_back(ns);
			}
		for (auto& x : times)
			std::sort(x.begin(), x.end());
		double x = times[0][5], y = times[1][5];
		std::printf("index_%u baseline=%.2f candidate=%.2f ns saving=%.2f%% (enqueue+drain, notification stub)\n", count, x, y, 100 * (1 - y / x));
	}
}
