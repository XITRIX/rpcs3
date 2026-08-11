#pragma once

#include "RPCS3IOS.h"

#include "util/types.hpp"

namespace rpcs3::ios
{
// Resets the per-session frame baseline when RPCS3 creates a new graphics
// frame. This does not retain or own the renderer.
void reset_performance_metrics();

// Called from the RSX presentation thread after a frame has been queued for
// display. rsx_load is RPCS3's approximate renderer utilization, not a Metal
// device-wide hardware counter.
void record_presented_frame(u32 rsx_load) noexcept;

// Returns the current dirty-memory headroom before iOS applies the process
// limit. The query is intentionally cheap enough for frame-boundary sampling.
u64 available_process_memory_headroom() noexcept;

// Produces an observational snapshot without taking the serialized lifecycle
// lock. The caller supplies struct_size for ABI validation.
rpcs3_ios_status capture_performance_metrics(rpcs3_ios_performance_metrics* metrics);
}
