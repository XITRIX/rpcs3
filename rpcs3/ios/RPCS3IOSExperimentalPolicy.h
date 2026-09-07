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
	bool dma_copy_specialization = false;
	bool texture_hash_hybrid = false;
	u32 fifo_cache_bytes = 1024;
	bool fifo_idle_wfe = false;
	bool deferred_get_publishing = false;
	bool getllar_backoff = false;
	bool rsx_dma_wait_parking = false;
	bool vulkan_command_buffer_reclamation = false;
	bool expanded_spu_scratch = false;
	bool persistent_spu_object_cache = false;
};

// Replaced only while boot is serialized and before emulation workers exist.
// Consumers either snapshot fields in their constructor or configure a hot-path
// function pointer from resolve_experimental_policy().
const experimental_policy& get_experimental_policy() noexcept;
void resolve_experimental_policy() noexcept;

// The generated ARM64 SPU gateways are process-lifetime functions, while the
// effective experimental policy is resolved once per boot. Keep the immediate
// choice behind this tiny accessor so a later boot can return to the upstream
// 8 KiB reservation without rebuilding the gateway.
u32 get_spu_gateway_scratch_size() noexcept;
}
