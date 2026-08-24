#pragma once

#include <cstdint>

namespace rpcs3::ios
{
	enum class process_memory_pressure : std::uint8_t
	{
		low,
		moderate,
		severe,
		fatal,
	};

	inline constexpr std::uint64_t process_memory_mib = 0x100000ull;
	inline constexpr std::uint32_t automatic_llvm_compile_threads = 3;
	inline constexpr std::uint32_t savestate_compression_threads = 3;

	// LLVM compilation has a large transient working set. On iOS, a zero
	// configuration value means a memory-safe automatic limit instead of every
	// performance core; an explicit value remains an opt-in override.
	constexpr std::uint32_t get_llvm_compile_thread_limit(
		std::uint32_t available_threads,
		std::uint32_t configured_threads)
	{
		const std::uint32_t hardware_limit = available_threads ? available_threads : 1;
		const std::uint32_t requested_threads = configured_threads
			? configured_threads
			: automatic_llvm_compile_threads;
		return requested_threads < hardware_limit ? requested_threads : hardware_limit;
	}

	// Every ZSTD savestate worker can temporarily own both an uncompressed input
	// block and its compression-bound output. Keep enough parallelism to avoid a
	// single-threaded multi-gigabyte save without scaling that transient working
	// set to every performance core on unified-memory devices.
	constexpr std::uint32_t get_savestate_compression_thread_limit(
		std::uint32_t available_threads)
	{
		const std::uint32_t worker_limit = available_threads > 1 ? available_threads - 1 : 1;
		return worker_limit < savestate_compression_threads
			? worker_limit
			: savestate_compression_threads;
	}

	// iOS reports the process's current dirty-memory allowance rather than a
	// fixed device-wide limit. Enter pressure states immediately, but require an
	// additional 256 MiB before relaxing them so cache reclamation cannot flap at
	// a threshold from one frame to the next.
	inline constexpr std::uint64_t process_headroom_moderate_enter = 1536 * process_memory_mib;
	inline constexpr std::uint64_t process_headroom_moderate_exit = 1792 * process_memory_mib;
	inline constexpr std::uint64_t process_headroom_severe_enter = 1024 * process_memory_mib;
	inline constexpr std::uint64_t process_headroom_severe_exit = 1280 * process_memory_mib;
	inline constexpr std::uint64_t process_headroom_fatal_enter = 512 * process_memory_mib;
	inline constexpr std::uint64_t process_headroom_fatal_exit = 768 * process_memory_mib;
	inline constexpr std::uint64_t texture_cache_quota_moderate = 384 * process_memory_mib;
	inline constexpr std::uint64_t texture_cache_quota_severe = 256 * process_memory_mib;
	inline constexpr std::uint64_t texture_cache_quota_fatal = 128 * process_memory_mib;

	constexpr process_memory_pressure get_process_memory_pressure(
		std::uint64_t available_bytes,
		process_memory_pressure current = process_memory_pressure::low)
	{
		process_memory_pressure entered = process_memory_pressure::low;
		if (available_bytes <= process_headroom_fatal_enter)
		{
			entered = process_memory_pressure::fatal;
		}
		else if (available_bytes <= process_headroom_severe_enter)
		{
			entered = process_memory_pressure::severe;
		}
		else if (available_bytes <= process_headroom_moderate_enter)
		{
			entered = process_memory_pressure::moderate;
		}

		if (entered > current)
		{
			return entered;
		}

		switch (current)
		{
		case process_memory_pressure::fatal:
			if (available_bytes <= process_headroom_fatal_exit)
			{
				return process_memory_pressure::fatal;
			}
			[[fallthrough]];
		case process_memory_pressure::severe:
			if (available_bytes <= process_headroom_severe_exit)
			{
				return process_memory_pressure::severe;
			}
			[[fallthrough]];
		case process_memory_pressure::moderate:
			if (available_bytes <= process_headroom_moderate_exit)
			{
				return process_memory_pressure::moderate;
			}
			[[fallthrough]];
		case process_memory_pressure::low:
			return entered;
		}

		return entered;
	}

	constexpr bool has_safe_savestate_headroom(std::uint64_t available_bytes)
	{
		// Streaming compression is bounded, but committing guest/RSX state can
		// still temporarily consume hundreds of MiB. Do not destructively stop a
		// live game to save once the process has entered severe pressure.
		return available_bytes > process_headroom_severe_enter;
	}

	constexpr std::uint64_t get_process_pressure_texture_cache_quota(
		std::uint64_t normal_quota_bytes,
		process_memory_pressure pressure)
	{
		switch (pressure)
		{
		case process_memory_pressure::low:
			return normal_quota_bytes;
		case process_memory_pressure::moderate:
			return normal_quota_bytes < texture_cache_quota_moderate
				? normal_quota_bytes
				: texture_cache_quota_moderate;
		case process_memory_pressure::severe:
			return normal_quota_bytes < texture_cache_quota_severe
				? normal_quota_bytes
				: texture_cache_quota_severe;
		case process_memory_pressure::fatal:
			return normal_quota_bytes < texture_cache_quota_fatal
				? normal_quota_bytes
				: texture_cache_quota_fatal;
		}

		return texture_cache_quota_fatal;
	}
}
