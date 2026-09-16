#pragma once

#include "IOSAudioRecovery.h"
#include <atomic>
#include <limits>

namespace rpcs3::ios::audio_detail
{
enum class callback_outcome { rendered, inactive, lock_miss };

// Cumulative counters: snapshots may span an in-flight callback, but no reset or
// drain races can lose events. One callback writer; readers never block it.
struct callback_snapshot
{
	std::uint64_t callbacks, requested, delivered, filler, short_reads, zero_reads;
	std::uint64_t inactive, lock_misses, min_frames, max_frames, max_gap_us, late_callbacks;
};

class callback_diagnostics final
{
public:
	static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

	void reset_timing() noexcept { m_reset_timing.store(true, std::memory_order_relaxed); }
	void record(std::uint32_t requested, std::uint32_t delivered, callback_outcome outcome,
		std::uint64_t now_us, std::uint32_t rate) noexcept
	{
		if (m_reset_timing.exchange(false, std::memory_order_relaxed))
		{
			m_previous_us = 0;
			m_previous_frames = 0;
		}
		delivered = outcome == callback_outcome::rendered ? std::min(requested, delivered) : 0;
		add(m_callbacks, 1); add(m_requested, requested); add(m_delivered, delivered);
		add(m_filler, requested - delivered);
		if (outcome == callback_outcome::inactive) add(m_inactive, 1);
		else if (outcome == callback_outcome::lock_miss) add(m_lock_misses, 1);
		else if (delivered < requested)
		{
			add(m_short_reads, 1);
			if (!delivered) add(m_zero_reads, 1);
		}
		minimum(m_min_frames, requested); maximum(m_max_frames, requested);
		if (outcome != callback_outcome::inactive && m_previous_us && now_us >= m_previous_us && rate)
		{
			const auto gap = now_us - m_previous_us;
			maximum(m_max_gap_us, gap);
			const auto expected = static_cast<std::uint64_t>(m_previous_frames) * 1'000'000 / rate;
			if (gap > expected + expected / 2 + 1000) add(m_late_callbacks, 1);
		}
		m_previous_us = outcome == callback_outcome::inactive ? 0 : now_us;
		m_previous_frames = requested;
	}
	callback_snapshot snapshot() const noexcept
	{
		return {get(m_callbacks), get(m_requested), get(m_delivered), get(m_filler), get(m_short_reads), get(m_zero_reads),
			get(m_inactive), get(m_lock_misses), minimum_value(m_min_frames), get(m_max_frames), get(m_max_gap_us), get(m_late_callbacks)};
	}

	static std::uint64_t get(const std::atomic<std::uint64_t>& v) noexcept { return v.load(std::memory_order_relaxed); }
	static void add(std::atomic<std::uint64_t>& v, std::uint64_t n) noexcept { v.fetch_add(n, std::memory_order_relaxed); }
	// Single-writer extrema; snapshots only load, never exchange/reset.
	static void maximum(std::atomic<std::uint64_t>& v, std::uint64_t n) noexcept { if (n > get(v)) v.store(n, std::memory_order_relaxed); }
	static void minimum(std::atomic<std::uint64_t>& v, std::uint64_t n) noexcept { if (n < get(v)) v.store(n, std::memory_order_relaxed); }
	static std::uint64_t minimum_value(const std::atomic<std::uint64_t>& v) noexcept
	{
		const auto n = get(v);
		return n == std::numeric_limits<std::uint64_t>::max() ? 0 : n;
	}

private:
	std::atomic<std::uint64_t> m_callbacks{}, m_requested{}, m_delivered{}, m_filler{}, m_short_reads{}, m_zero_reads{};
	std::atomic<std::uint64_t> m_inactive{}, m_lock_misses{}, m_max_frames{}, m_max_gap_us{}, m_late_callbacks{};
	std::atomic<std::uint64_t> m_min_frames{std::numeric_limits<std::uint64_t>::max()};
	std::atomic_bool m_reset_timing = false;
	std::uint64_t m_previous_us = 0;
	std::uint32_t m_previous_frames = 0;
};

struct queue_snapshot
{
	std::uint64_t requested, delivered, starved_frames, refill_frames, min_fill, max_fill;
	std::uint64_t recoveries, resumes, timeouts;
};

class queue_diagnostics final
{
public:
	// Only the producer accesses these. Forced prefill is separate from game silence.
	std::uint64_t mix_blocks = 0, silence_blocks = 0, prefill_blocks = 0, not_ready_blocks = 0;
	std::uint64_t rejected_blocks = 0, rejected_frames = 0;
	std::uint64_t untouched_waits = 0, in_progress_waits = 0, timeout_advances = 0, incomplete_mixes = 0;
	std::uint64_t no_port_silence = 0, untouched_silence = 0;
	std::uint64_t missing_blocks = 0, tempo_updates = 0, input_rate_ppm = 1'000'000;
	std::uint64_t target_us = 0, period_us = 0, tempo_ppm = 1'000'000;
	std::uint32_t active_ports = 0, untouched_ports = 0;

	void record_read(std::uint32_t requested, std::uint32_t available, const refill_result& result) noexcept
	{
		using d = callback_diagnostics;
		d::add(m_requested, requested); d::add(m_delivered, result.frames);
		d::add(result.held ? m_refill_frames : m_starved_frames, requested - result.frames);
		d::minimum(m_min_fill, available - std::min(available, result.frames));
		d::maximum(m_max_fill, available);
		if (result.started) d::add(m_recoveries, 1);
		if (result.resumed) d::add(m_resumes, 1);
		if (result.timed_out) d::add(m_timeouts, 1);
	}
	void record_write(std::uint32_t requested, std::uint32_t accepted) noexcept
	{
		if (accepted < requested) { ++rejected_blocks; rejected_frames += requested - accepted; }
	}
	queue_snapshot snapshot() const noexcept
	{
		using d = callback_diagnostics;
		return {d::get(m_requested), d::get(m_delivered), d::get(m_starved_frames), d::get(m_refill_frames),
			d::minimum_value(m_min_fill), d::get(m_max_fill), d::get(m_recoveries), d::get(m_resumes), d::get(m_timeouts)};
	}

private:
	std::atomic<std::uint64_t> m_requested{}, m_delivered{}, m_starved_frames{}, m_refill_frames{}, m_max_fill{};
	std::atomic<std::uint64_t> m_min_fill{std::numeric_limits<std::uint64_t>::max()};
	std::atomic<std::uint64_t> m_recoveries{}, m_resumes{}, m_timeouts{};
};
}
