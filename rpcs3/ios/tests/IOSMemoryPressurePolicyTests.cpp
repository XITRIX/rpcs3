#include "rpcs3/ios/IOSMemoryPressurePolicy.h"

#include <cassert>

using rpcs3::ios::process_memory_mib;
using rpcs3::ios::process_memory_pressure;

int main()
{
	using rpcs3::ios::get_llvm_compile_thread_limit;
	using rpcs3::ios::get_ppu_compile_file_budget;
	using rpcs3::ios::get_process_memory_pressure;
	using rpcs3::ios::get_process_pressure_texture_cache_quota;
	using rpcs3::ios::get_savestate_compression_thread_limit;
	using rpcs3::ios::has_safe_savestate_headroom;

	static_assert(get_llvm_compile_thread_limit(10, 0) == 3);
	static_assert(get_llvm_compile_thread_limit(2, 0) == 2);
	static_assert(get_llvm_compile_thread_limit(10, 6) == 6);
	static_assert(get_llvm_compile_thread_limit(4, 12) == 4);
	static_assert(get_llvm_compile_thread_limit(0, 0) == 1);
	static_assert(get_ppu_compile_file_budget(16ull << 30, 0) == 65536);
	static_assert(get_ppu_compile_file_budget(0, 16ull << 30) == 65536);
	static_assert(get_ppu_compile_file_budget(16ull << 30, 1) == 65536);
	static_assert(get_ppu_compile_file_budget(16ull << 30, 1ull << 30) == 402654);
	static_assert(get_ppu_compile_file_budget(16ull << 30, 4ull << 30) == 1610613);
	static_assert(get_ppu_compile_file_budget(1ull << 30, 16ull << 30) == 402654);
	static_assert(get_ppu_compile_file_budget(UINT64_MAX, UINT64_MAX) == UINT32_MAX);
	// Increasing headroom must never reduce admission capacity. The nonzero
	// minimum preserves the queue's existing admission of one oversized file.
	std::uint32_t previous_budget = 0;
	for (std::uint64_t mib = 0; mib <= 32768; ++mib)
	{
		const auto budget = get_ppu_compile_file_budget(16ull << 30, mib * process_memory_mib);
		assert(budget >= 65536 && budget >= previous_budget);
		previous_budget = budget;
	}
	static_assert(get_savestate_compression_thread_limit(0) == 1);
	static_assert(get_savestate_compression_thread_limit(1) == 1);
	static_assert(get_savestate_compression_thread_limit(2) == 1);
	static_assert(get_savestate_compression_thread_limit(4) == 3);
	static_assert(get_savestate_compression_thread_limit(10) == 3);

	static_assert(get_process_memory_pressure(2'000 * process_memory_mib) == process_memory_pressure::low);
	static_assert(get_process_memory_pressure(1536 * process_memory_mib) == process_memory_pressure::moderate);
	static_assert(get_process_memory_pressure(1024 * process_memory_mib) == process_memory_pressure::severe);
	static_assert(get_process_memory_pressure(512 * process_memory_mib) == process_memory_pressure::fatal);
	static_assert(get_process_memory_pressure(0) == process_memory_pressure::fatal);

	// A state relaxes only after recovering the extra 256 MiB hysteresis band.
	static_assert(get_process_memory_pressure(700 * process_memory_mib, process_memory_pressure::fatal) == process_memory_pressure::fatal);
	static_assert(get_process_memory_pressure(900 * process_memory_mib, process_memory_pressure::fatal) == process_memory_pressure::severe);
	static_assert(get_process_memory_pressure(1200 * process_memory_mib, process_memory_pressure::severe) == process_memory_pressure::severe);
	static_assert(get_process_memory_pressure(1400 * process_memory_mib, process_memory_pressure::severe) == process_memory_pressure::moderate);
	static_assert(get_process_memory_pressure(1700 * process_memory_mib, process_memory_pressure::moderate) == process_memory_pressure::moderate);
	static_assert(get_process_memory_pressure(1800 * process_memory_mib, process_memory_pressure::moderate) == process_memory_pressure::low);

	static_assert(!has_safe_savestate_headroom(1024 * process_memory_mib));
	static_assert(has_safe_savestate_headroom(1024 * process_memory_mib + 1));
	static_assert(get_process_pressure_texture_cache_quota(559 * process_memory_mib, process_memory_pressure::low) == 559 * process_memory_mib);
	static_assert(get_process_pressure_texture_cache_quota(559 * process_memory_mib, process_memory_pressure::moderate) == 384 * process_memory_mib);
	static_assert(get_process_pressure_texture_cache_quota(559 * process_memory_mib, process_memory_pressure::severe) == 256 * process_memory_mib);
	static_assert(get_process_pressure_texture_cache_quota(559 * process_memory_mib, process_memory_pressure::fatal) == 128 * process_memory_mib);
	static_assert(get_process_pressure_texture_cache_quota(96 * process_memory_mib, process_memory_pressure::fatal) == 96 * process_memory_mib);

	assert(get_process_memory_pressure(400 * process_memory_mib, process_memory_pressure::moderate) == process_memory_pressure::fatal);
}
