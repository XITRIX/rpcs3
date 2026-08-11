#include "stdafx.h"
#include "RPCS3IOSPerformance.h"

#include "util/cpu_stats.hpp"
#include "util/sysinfo.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>

#ifdef __APPLE__
#include <mach/mach.h>
#endif

#ifdef RPCS3_IOS
#include <os/proc.h>
#endif

namespace
{
using sample_clock = std::chrono::steady_clock;

u64 process_memory_footprint()
{
#ifdef __APPLE__
	task_vm_info_data_t info{};
	mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
	if (task_info(mach_task_self(), TASK_VM_INFO,
		reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
	{
		return info.phys_footprint;
	}
	return 0;
#else
	return utils::get_memory_usage().second;
#endif
}

class performance_registry final
{
public:
	void reset()
	{
		std::lock_guard lock(m_sample_mutex);
		m_presented_frames = 0;
		m_rsx_load = 0;
		m_has_rsx_load = false;
		m_has_fps_baseline = false;
		m_last_frame_count = 0;
		m_last_sample_time = {};
		static_cast<void>(m_cpu_stats.get_usage());
	}

	void record_presented_frame(u32 rsx_load) noexcept
	{
		m_rsx_load.store(std::min(rsx_load, 100u), std::memory_order_relaxed);
		m_has_rsx_load.store(true, std::memory_order_release);
		m_presented_frames.fetch_add(1, std::memory_order_relaxed);
	}

	rpcs3_ios_status snapshot(rpcs3_ios_performance_metrics* metrics)
	{
		if (!metrics || metrics->struct_size < sizeof(rpcs3_ios_performance_metrics))
		{
			return RPCS3_IOS_INVALID_ARGUMENT;
		}

		const u64 memory_used = process_memory_footprint();
		const u64 memory_total = utils::get_total_memory();

		std::lock_guard lock(m_sample_mutex);
		const auto now = sample_clock::now();
		const u64 frame_count = m_presented_frames.load(std::memory_order_relaxed);
		const bool presented_since_last_sample = !m_has_fps_baseline || frame_count > m_last_frame_count;
		rpcs3_ios_performance_metrics result{};
		result.struct_size = sizeof(result);
		result.cpu_usage_percent = std::clamp(m_cpu_stats.get_usage(), 0.0, 100.0);
		result.valid_fields |= RPCS3_IOS_PERFORMANCE_CPU_VALID;

		if (m_has_fps_baseline)
		{
			const double elapsed = std::chrono::duration<double>(now - m_last_sample_time).count();
			if (elapsed > 0.0 && elapsed <= 2.0 && frame_count >= m_last_frame_count)
			{
				result.frames_per_second = static_cast<double>(frame_count - m_last_frame_count) / elapsed;
				result.valid_fields |= RPCS3_IOS_PERFORMANCE_FPS_VALID;
			}
		}

		m_has_fps_baseline = true;
		m_last_frame_count = frame_count;
		m_last_sample_time = now;

		if (m_has_rsx_load.load(std::memory_order_acquire))
		{
			result.gpu_usage_percent = presented_since_last_sample
				? m_rsx_load.load(std::memory_order_relaxed)
				: 0.0;
			result.valid_fields |= RPCS3_IOS_PERFORMANCE_GPU_VALID;
		}

		if (memory_used && memory_total)
		{
			result.memory_used_bytes = memory_used;
			result.memory_total_bytes = memory_total;
			result.valid_fields |= RPCS3_IOS_PERFORMANCE_MEMORY_VALID;
		}

		*metrics = result;
		return RPCS3_IOS_OK;
	}

private:
	std::atomic<u64> m_presented_frames{0};
	std::atomic<u32> m_rsx_load{0};
	std::atomic_bool m_has_rsx_load{false};
	std::mutex m_sample_mutex;
	utils::cpu_stats m_cpu_stats;
	bool m_has_fps_baseline = false;
	u64 m_last_frame_count = 0;
	sample_clock::time_point m_last_sample_time{};
};

performance_registry g_performance_registry;
}

namespace rpcs3::ios
{
void reset_performance_metrics()
{
	g_performance_registry.reset();
}

void record_presented_frame(u32 rsx_load) noexcept
{
	g_performance_registry.record_presented_frame(rsx_load);
}

u64 available_process_memory_headroom() noexcept
{
#ifdef RPCS3_IOS
	return os_proc_available_memory();
#else
	return umax;
#endif
}

rpcs3_ios_status capture_performance_metrics(rpcs3_ios_performance_metrics* metrics)
{
	return g_performance_registry.snapshot(metrics);
}
}
