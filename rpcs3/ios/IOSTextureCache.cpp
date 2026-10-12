#include "stdafx.h"
#include "IOSTextureCache.h"
#include "Emu/RSX/Common/texture_cache_protection_policy.h"
#include "Emu/system_config.h"
#include "Utilities/JITIOS.h"
#include "util/logs.hpp"

#include <algorithm>
#include <mach/mach_time.h>
#include <sys/sysctl.h>
#include <unistd.h>

LOG_CHANNEL(texture_validation_log, "Texture Validation");

namespace rpcs3::ios
{
texture_cache_metrics g_texture_validation_metrics;
namespace
{
bool s_write_tracking = false;
double s_ns_per_tick = 0;
std::uint64_t s_last_report = 0;
texture_cache_metrics::snapshot s_previous;

bool debugger_known_detached() noexcept
{
	if (g_cfg.core.external_debugger) return false;
	int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
	kinfo_proc info{};
	size_t size = sizeof(info);
	// Unlike the general debugger helper, a failed query must fail closed.
	return sysctl(mib, 4, &info, &size, nullptr, 0) == 0 && size == sizeof(info) &&
		!(info.kp_proc.p_flag & P_TRACED);
}

void report(bool final)
{
	auto& metrics = g_texture_validation_metrics;
	if (!metrics.enabled()) return;
	const auto now = mach_absolute_time();
	const double elapsed_ms = (now - s_last_report) * s_ns_per_tick / 1e6;
	if (!final && elapsed_ms < 5000.0) return;
	const auto current = metrics.read();
	using m = texture_cache_metrics;
	const auto delta = [&](m::counter c) { return current.totals[c] - s_previous.totals[c]; };
	const auto frames = delta(m::presents);
	const auto calls = delta(m::baseline_calls) + delta(m::validation_calls);
	const auto hash_bytes = delta(m::bytes);
	const double hash_ms = delta(m::ticks) * s_ns_per_tick / 1e6;
	texture_validation_log.notice(
		"window final=%d elapsed_ms=%.3f presents=%llu baseline=%llu validations=%llu bytes=%llu hash_elapsed_ms=%.3f mismatches=%llu locked_checks=%llu read_faults=%llu write_faults=%llu unhandled_faults=%llu range_drops=%llu calls_per_present=%.3f bytes_per_present=%.3f hash_ms_per_present=%.3f",
		final, elapsed_ms, frames, delta(m::baseline_calls), delta(m::validation_calls), hash_bytes, hash_ms,
		delta(m::mismatches), delta(m::locked_checks), delta(m::read_faults), delta(m::write_faults),
		delta(m::unhandled_faults), delta(m::range_drops), frames ? double(calls) / frames : 0.0,
		frames ? double(hash_bytes) / frames : 0.0, frames ? hash_ms / frames : 0.0);
	texture_validation_log.notice(
		"detail buckets_lt4k_lt16k_lt64k_lt256k_lt1m_lt4m_ge4m=%llu,%llu,%llu,%llu,%llu,%llu,%llu invalidations_read_write_unmap_reprotect_fbo=%llu,%llu,%llu,%llu,%llu",
		current.buckets[0] - s_previous.buckets[0], current.buckets[1] - s_previous.buckets[1],
		current.buckets[2] - s_previous.buckets[2], current.buckets[3] - s_previous.buckets[3],
		current.buckets[4] - s_previous.buckets[4], current.buckets[5] - s_previous.buckets[5],
		current.buckets[6] - s_previous.buckets[6], delta(m::invalidate_read), delta(m::invalidate_write),
		delta(m::invalidate_unmap), delta(m::invalidate_reprotect), delta(m::invalidate_fbo));
	std::array<std::size_t, m::range_capacity> order{};
	for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
	const auto range_bytes = [&](std::size_t i) { return current.ranges[i].bytes - s_previous.ranges[i].bytes; };
	std::partial_sort(order.begin(), order.begin() + 4, order.end(),
		[&](auto a, auto b) { return range_bytes(a) > range_bytes(b); });
	for (std::size_t rank = 0; rank < 4; ++rank)
	{
		const auto i = order[rank];
		const auto& r = current.ranges[i];
		const auto& old = s_previous.ranges[i];
		if (!r.key || !range_bytes(i)) continue;
		texture_validation_log.notice(
			"tracked_range address=0x%08x length=%u calls=%llu bytes=%llu hash_elapsed_ms=%.3f mismatches=%llu repeat_validations_within_present=%llu",
			static_cast<std::uint32_t>(r.key >> 32), static_cast<std::uint32_t>(r.key),
			r.calls - old.calls, range_bytes(i), (r.ticks - old.ticks) * s_ns_per_tick / 1e6,
			r.mismatches - old.mismatches, r.repeats - old.repeats);
	}
	s_previous = current;
	s_last_report = now;
}
}

void initialize_texture_cache_validation(bool request_tracking, bool diagnostics) noexcept
{
	const auto state = jit::get_statistics();
	const bool jit_ready = state.sealed && (state.backend == jit::arena_backend::legacy_debugger ||
		state.backend == jit::arena_backend::universal_mirrored);
	const bool detached = request_tracking && jit_ready && debugger_known_detached();
	s_write_tracking = rsx::allow_texture_write_tracking(request_tracking, jit_ready, detached);
	const char* backend = state.backend == jit::arena_backend::legacy_debugger ? "legacy" :
		state.backend == jit::arena_backend::universal_mirrored ? "universal" : "disabled";
	texture_validation_log.notice("policy requested=%d backend=%s jit_ready=%d debugger_known_detached=%d write_tracking=%d diagnostics=%d",
		request_tracking, backend, jit_ready, detached, s_write_tracking, diagnostics);
	if (request_tracking && !s_write_tracking)
		texture_validation_log.warning("Read-only write tracking unavailable for this JIT/debugger state; retaining full XXH3 hashing");
	mach_timebase_info_data_t timebase{};
	const bool clock_ready = mach_timebase_info(&timebase) == KERN_SUCCESS && timebase.denom;
	s_ns_per_tick = clock_ready ? double(timebase.numer) / timebase.denom : 0;
	s_last_report = mach_absolute_time();
	s_previous = {};
	g_texture_validation_metrics.reset(diagnostics && clock_ready);
	if (diagnostics)
		texture_validation_log.notice("Diagnostics use fixed 128-range storage, at most four probes/hash and six lines/5s plus shutdown. Hash time is summed elapsed time, not CPU time. Presents are successful Vulkan requests, not display completion; range_drops reports incomplete range detail.");
}

bool texture_write_tracking_enabled() noexcept { return s_write_tracking; }
std::uint64_t texture_hash_clock() noexcept { return mach_absolute_time(); }
void end_texture_hash(std::uint64_t start, std::uint32_t address, std::uint32_t length,
	bool validation, bool changed) noexcept
{
	if (start) g_texture_validation_metrics.record_hash(address, length, mach_absolute_time() - start, validation, changed);
}
void texture_validation_presented()
{
	g_texture_validation_metrics.increment(texture_cache_metrics::presents);
	report(false);
}
void finish_texture_cache_validation()
{
	report(true);
	g_texture_validation_metrics.disable();
}
}
