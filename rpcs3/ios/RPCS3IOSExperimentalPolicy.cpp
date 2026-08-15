#include "stdafx.h"
#include "RPCS3IOSExperimentalPolicy.h"

#include "Emu/RSX/Common/BufferUtils.h"
#include "Emu/system_config.h"
#include "util/logs.hpp"

LOG_CHANNEL(ios_experimental_log, "iOS Experimental");

namespace rpcs3::ios
{
namespace
{
experimental_policy s_policy{};

bool resolve_mode(ios_experimental_mode mode, bool automatic_value) noexcept
{
	switch (mode)
	{
	case ios_experimental_mode::automatic:
		return automatic_value;
	case ios_experimental_mode::enabled:
		return true;
	case ios_experimental_mode::disabled:
		return false;
	}

	return automatic_value;
}
}

const experimental_policy& get_experimental_policy() noexcept
{
	return s_policy;
}

void resolve_experimental_policy() noexcept
{
#ifdef ARCH_ARM64
	constexpr bool arm64_default = true;
#else
	constexpr bool arm64_default = false;
#endif

	experimental_policy resolved{};
	resolved.neon_byte_swap = resolve_mode(g_cfg.ios_experimental.neon_byte_swap, arm64_default);
	resolved.neon_primitive_restart = resolve_mode(g_cfg.ios_experimental.neon_primitive_restart, arm64_default);
	resolved.precomputed_indices = resolve_mode(g_cfg.ios_experimental.precomputed_indices, arm64_default);
	resolved.mobile_spu_scheduling = resolve_mode(g_cfg.ios_experimental.mobile_spu_scheduling, false);
	resolved.fifo_cache_bytes = g_cfg.ios_experimental.fifo_cache_size == ios_fifo_cache_size::_4_kib ? 4096 : 1024;
	resolved.fifo_idle_wfe = g_cfg.ios_experimental.fifo_idle_mode == ios_fifo_idle_mode::wait_for_event;
	resolved.deferred_get_publishing = resolve_mode(g_cfg.ios_experimental.deferred_get_publishing, false);
	resolved.getllar_backoff = resolve_mode(g_cfg.ios_experimental.getllar_backoff, false);
	resolved.persistent_spu_object_cache = resolve_mode(g_cfg.ios_experimental.persistent_spu_object_cache, false);

	s_policy = resolved;
	configure_buffer_optimizations(resolved.neon_byte_swap, resolved.neon_primitive_restart, resolved.precomputed_indices);

	ios_experimental_log.notice(
		"Resolved boot policy: swap=%d restart=%d precomputed=%d mobile_spu=%d fifo=%u wfe=%d deferred_get=%d getllar=%d spu_object_cache=%d",
		resolved.neon_byte_swap, resolved.neon_primitive_restart, resolved.precomputed_indices,
		resolved.mobile_spu_scheduling, resolved.fifo_cache_bytes, resolved.fifo_idle_wfe,
		resolved.deferred_get_publishing, resolved.getllar_backoff, resolved.persistent_spu_object_cache);
}
}
