#pragma once

#include "IOSTextureCacheMetrics.h"

namespace rpcs3::ios
{
// One renderer owns the process-wide policy/capture between serialized boots.
void initialize_texture_cache_validation(bool request_tracking, bool diagnostics) noexcept;
bool texture_write_tracking_enabled() noexcept;
extern texture_cache_metrics g_texture_validation_metrics;
inline texture_cache_metrics& texture_validation_metrics() noexcept { return g_texture_validation_metrics; }
std::uint64_t texture_hash_clock() noexcept;
inline std::uint64_t begin_texture_hash() noexcept
{
	return g_texture_validation_metrics.enabled() ? texture_hash_clock() : 0;
}
void end_texture_hash(std::uint64_t start, std::uint32_t address, std::uint32_t length,
	bool validation, bool changed) noexcept;
void texture_validation_presented(); // Accepted Vulkan presentations, not display callbacks.
void finish_texture_cache_validation();
}
