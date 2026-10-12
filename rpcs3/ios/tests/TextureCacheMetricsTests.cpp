#include "ios/IOSTextureCacheMetrics.h"
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>

int main()
{
	using m = rpcs3::ios::texture_cache_metrics;
	m metrics;
	metrics.record_hash(4096, 4096, 10, true, true);
	metrics.increment(m::write_faults);
	assert(metrics.read().totals[m::validation_calls] == 0);
	assert(metrics.read().totals[m::write_faults] == 0);
	metrics.reset(true);
	metrics.record_hash(4096, 4096, 10, false, true);
	metrics.record_hash(4096, 4096, 20, true, false);
	metrics.record_hash(4096, 4096, 30, true, true);
	metrics.increment(m::presents);
	metrics.record_hash(4096, 4096, 40, true, false);
	metrics.increment(m::locked_checks);
	metrics.increment(m::write_faults);
	auto snapshot = metrics.read();
	assert(snapshot.totals[m::baseline_calls] == 1);
	assert(snapshot.totals[m::validation_calls] == 3);
	assert(snapshot.totals[m::bytes] == 4 * 4096);
	assert(snapshot.totals[m::ticks] == 100);
	assert(snapshot.totals[m::mismatches] == 1);
	assert(snapshot.totals[m::presents] == 1);
	assert(snapshot.totals[m::locked_checks] == 1);
	assert(snapshot.totals[m::write_faults] == 1);
	std::uint64_t repeats = 0;
	for (const auto& range : snapshot.ranges) repeats += range.repeats;
	assert(repeats == 1);
	for (const auto size : {0u, 4095u, 4096u, 16383u, 16384u, 65535u, 65536u,
		262143u, 262144u, 1048575u, 1048576u, 4194303u, 4194304u, 0xffffffffu})
		assert(m::bucket(size) < m::bucket_count);
	assert(m::bucket(4095) == 0 && m::bucket(4096) == 1 && m::bucket(4194304) == 6);
	metrics.reset(true);
	// Contention and deliberate table overflow must not lose global totals.
	std::vector<std::thread> workers;
	for (unsigned thread = 0; thread < 4; ++thread)
		workers.emplace_back([&]
		{
			for (unsigned i = 0; i < 50000; ++i)
			{
				metrics.record_hash((i % 1000) * 4096, 4096, 17, true, i % 5 == 0);
				metrics.increment(m::write_faults);
			}
		});
	for (auto& worker : workers) worker.join();
	snapshot = metrics.read();
	assert(snapshot.totals[m::validation_calls] == 200000);
	assert(snapshot.totals[m::bytes] == 200000ull * 4096);
	assert(snapshot.totals[m::ticks] == 200000ull * 17);
	assert(snapshot.totals[m::mismatches] == 40000);
	assert(snapshot.totals[m::write_faults] == 200000);
	assert(snapshot.buckets[1] == 200000);
	assert(snapshot.totals[m::range_drops] > 0);
	std::uint64_t tracked = 0;
	for (const auto& range : snapshot.ranges) tracked += range.calls;
	assert(tracked + snapshot.totals[m::range_drops] == 200000);
	metrics.disable();
	metrics.record_hash(0, 4096, 99, true, true);
	metrics.increment(m::presents);
	assert(metrics.read().totals == snapshot.totals);
	metrics.reset(false);
	snapshot = metrics.read();
	for (const auto value : snapshot.totals) assert(value == 0);
	for (const auto value : snapshot.buckets) assert(value == 0);
	for (const auto& range : snapshot.ranges) assert(range.key == 0 && range.calls == 0);
	std::puts("Texture metrics: disabled/reset, exact totals, bucket boundaries, repeated ranges and 200000 concurrent hashes passed");
}
