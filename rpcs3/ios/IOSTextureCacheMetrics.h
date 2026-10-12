#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace rpcs3::ios
{
// Fixed storage, bounded probing, no allocation/locks/logging on writer or
// fault paths. Totals include all hashes; range detail is explicitly partial
// when its table is full. Snapshots are observational, not a global barrier.
class texture_cache_metrics
{
public:
	enum counter : std::size_t
	{
		baseline_calls, validation_calls, bytes, ticks, mismatches, locked_checks,
		read_faults, write_faults, unhandled_faults, range_drops, presents,
		invalidate_read, invalidate_write, invalidate_unmap, invalidate_reprotect, invalidate_fbo,
		count
	};
	static constexpr std::size_t range_capacity = 128;
	static constexpr std::size_t bucket_count = 7;
	struct range_snapshot
	{
		std::uint64_t key = 0, calls = 0, bytes = 0, ticks = 0, mismatches = 0, repeats = 0;
	};
	struct snapshot
	{
		std::array<std::uint64_t, count> totals{};
		std::array<std::uint64_t, bucket_count> buckets{};
		std::array<range_snapshot, range_capacity> ranges{};
	};

private:
	static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
	static_assert(std::atomic<bool>::is_always_lock_free);
	struct range_counters
	{
		std::atomic<std::uint64_t> key{0}, calls{0}, bytes{0}, ticks{0}, mismatches{0}, repeats{0};
		std::atomic<std::uint64_t> last_present{std::numeric_limits<std::uint64_t>::max()};
	};
	std::atomic<bool> m_enabled{false};
	std::array<std::atomic<std::uint64_t>, count> m_totals{};
	std::array<std::atomic<std::uint64_t>, bucket_count> m_buckets{};
	std::array<range_counters, range_capacity> m_ranges{};
	static constexpr auto relaxed = std::memory_order_relaxed;

public:
	bool enabled() const noexcept { return m_enabled.load(relaxed); }
	void disable() noexcept { m_enabled.store(false, relaxed); }
	// Only at serialized renderer construction, before any workers can record.
	void reset(bool enabled) noexcept
	{
		disable();
		for (auto& value : m_totals) value.store(0, relaxed);
		for (auto& value : m_buckets) value.store(0, relaxed);
		for (auto& range : m_ranges)
		{
			range.key.store(0, relaxed);
			range.calls.store(0, relaxed);
			range.bytes.store(0, relaxed);
			range.ticks.store(0, relaxed);
			range.mismatches.store(0, relaxed);
			range.repeats.store(0, relaxed);
			range.last_present.store(std::numeric_limits<std::uint64_t>::max(), relaxed);
		}
		m_enabled.store(enabled, relaxed);
	}
	void increment(counter value) noexcept
	{
		if (enabled()) m_totals[value].fetch_add(1, relaxed);
	}
	static constexpr std::size_t bucket(std::uint32_t size) noexcept
	{
		return size < 4096 ? 0 : size < 16384 ? 1 : size < 65536 ? 2 :
			size < 262144 ? 3 : size < 1048576 ? 4 : size < 4194304 ? 5 : 6;
	}
	void record_hash(std::uint32_t address, std::uint32_t length, std::uint64_t elapsed_ticks,
		bool validation, bool changed) noexcept
	{
		if (!enabled()) return;
		m_totals[validation ? validation_calls : baseline_calls].fetch_add(1, relaxed);
		m_totals[bytes].fetch_add(length, relaxed);
		m_totals[ticks].fetch_add(elapsed_ticks, relaxed);
		m_buckets[bucket(length)].fetch_add(1, relaxed);
		if (validation && changed) m_totals[mismatches].fetch_add(1, relaxed);
		// Real texture ranges have nonzero length; zero remains the empty key.
		const auto key = (std::uint64_t{address} << 32) | length;
		if (!length) return;
		const auto mixed = ((key ^ (key >> 23)) * 0x9e3779b97f4a7c15ull);
		const auto first = static_cast<std::size_t>(mixed >> 57);
		for (std::size_t probe = 0; probe < 4; ++probe)
		{
			auto& range = m_ranges[(first + probe) % range_capacity];
			auto found = range.key.load(relaxed);
			if (!found && !range.key.compare_exchange_strong(found, key, relaxed))
			{
				if (found != key) continue;
			}
			else if (found && found != key) continue;
			range.calls.fetch_add(1, relaxed);
			range.bytes.fetch_add(length, relaxed);
			range.ticks.fetch_add(elapsed_ticks, relaxed);
			if (validation && changed) range.mismatches.fetch_add(1, relaxed);
			// Repeats count validations of this range in one accepted-present
			// interval, not proof that guest memory remained unchanged.
			const auto frame = m_totals[presents].load(relaxed);
			if (validation && range.last_present.exchange(frame, relaxed) == frame)
				range.repeats.fetch_add(1, relaxed);
			return;
		}
		m_totals[range_drops].fetch_add(1, relaxed);
	}
	snapshot read() const noexcept
	{
		snapshot result;
		for (std::size_t i = 0; i < count; ++i) result.totals[i] = m_totals[i].load(relaxed);
		for (std::size_t i = 0; i < bucket_count; ++i) result.buckets[i] = m_buckets[i].load(relaxed);
		for (std::size_t i = 0; i < range_capacity; ++i)
		{
			const auto& r = m_ranges[i];
			result.ranges[i] = {r.key.load(relaxed), r.calls.load(relaxed), r.bytes.load(relaxed),
				r.ticks.load(relaxed), r.mismatches.load(relaxed), r.repeats.load(relaxed)};
		}
		return result;
	}
};
static_assert(sizeof(texture_cache_metrics) < 16 * 1024);
}
