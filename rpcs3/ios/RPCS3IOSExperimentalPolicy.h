#pragma once

#include "util/types.hpp"

namespace rpcs3::ios
{
struct experimental_policy
{
	bool neon_byte_swap = false;
	bool neon_primitive_restart = false;
	bool precomputed_indices = false;
	bool mobile_spu_scheduling = false;
	u32 fifo_cache_bytes = 1024;
	bool fifo_idle_wfe = false;
	bool deferred_get_publishing = false;
	bool getllar_backoff = false;
	bool persistent_spu_object_cache = false;
};

// Replaced only while boot is serialized and before emulation workers exist.
// Consumers either snapshot fields in their constructor or configure a hot-path
// function pointer from resolve_experimental_policy().
const experimental_policy& get_experimental_policy() noexcept;
void resolve_experimental_policy() noexcept;
}
