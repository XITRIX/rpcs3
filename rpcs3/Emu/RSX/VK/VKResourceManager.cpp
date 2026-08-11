#include "stdafx.h"
#include "VKResourceManager.h"
#include "VKGSRender.h"
#include "VKCommandStream.h"
#include "vkutils/VRAMBudgetPolicy.h"

#ifdef RPCS3_IOS
#include "ios/IOSMemoryPressurePolicy.h"
#include "ios/RPCS3IOSPerformance.h"
#endif

namespace vk
{
	struct vmm_memory_stats
	{
		std::unordered_map<uptr, vmm_allocation_t> allocations;
		std::unordered_map<uptr, atomic_t<u64>> memory_usage;
		std::unordered_map<vmm_allocation_pool, atomic_t<u64>> pool_usage;
		shared_mutex mutex;

		void clear()
		{
			if (!allocations.empty())
			{
				rsx_log.error("Leaking memory allocations!");
				for (auto& leak : allocations)
				{
					rsx_log.error("Memory handle 0x%llx (%llu bytes) allocated from pool %d was not freed.",
						leak.first, leak.second.size, static_cast<int>(leak.second.pool));
				}
			}

			allocations.clear();
			memory_usage.clear();
			pool_usage.clear();
		}
	}
	g_vmm_stats;

	resource_manager g_resource_manager;
	atomic_t<u64> g_event_ctr;
	atomic_t<u64> g_last_completed_event;

#ifdef RPCS3_IOS
	struct ios_process_memory_pressure_state
	{
		rpcs3::ios::process_memory_pressure process_severity = rpcs3::ios::process_memory_pressure::low;
		rsx::problem_severity combined_severity = rsx::problem_severity::low;
		std::chrono::steady_clock::time_point next_report{};
	} g_ios_process_memory_pressure;

	static rsx::problem_severity to_problem_severity(rpcs3::ios::process_memory_pressure severity)
	{
		switch (severity)
		{
		case rpcs3::ios::process_memory_pressure::low: return rsx::problem_severity::low;
		case rpcs3::ios::process_memory_pressure::moderate: return rsx::problem_severity::moderate;
		case rpcs3::ios::process_memory_pressure::severe: return rsx::problem_severity::severe;
		case rpcs3::ios::process_memory_pressure::fatal: return rsx::problem_severity::fatal;
		}

		return rsx::problem_severity::fatal;
	}

	static const char* memory_severity_name(rsx::problem_severity severity)
	{
		switch (severity)
		{
		case rsx::problem_severity::low: return "low";
		case rsx::problem_severity::moderate: return "moderate";
		case rsx::problem_severity::severe: return "severe";
		case rsx::problem_severity::fatal: return "fatal";
		}

		return "unknown";
	}
#endif

	constexpr u64 s_vmm_warn_threshold_size = 2000 * 0x100000; // Warn if allocation on a single heap exceeds this value

	resource_manager* get_resource_manager()
	{
		return &g_resource_manager;
	}

	garbage_collector* get_gc()
	{
		return &g_resource_manager;
	}

	void resource_manager::trim()
	{
		// For any managed resources, try to keep the number of unused/idle resources as low as possible.
		// Improves search times as well as keeping us below the hardware limit.
		const auto& limits = get_current_renderer()->gpu().get_limits();
		const auto allocated_sampler_count = vmm_get_application_pool_usage(VMM_ALLOCATION_POOL_SAMPLER);
		const auto max_allowed_samplers = std::min((limits.maxSamplerAllocationCount * 3u) / 4u, 2048u);

		if (allocated_sampler_count > max_allowed_samplers)
		{
			ensure(max_allowed_samplers);
			rsx_log.warning("Trimming allocated samplers. Allocated = %u, Max = %u", allocated_sampler_count, limits.maxSamplerAllocationCount);

			auto filter_expr = [](const cached_sampler_object_t& sampler)
			{
				// Pick only where we have no ref
				return !sampler.has_refs();
			};

			for (auto& object : m_sampler_pool.collect(filter_expr))
			{
				dispose(object);
			}
		}
	}

	u64 get_event_id()
	{
		return g_event_ctr++;
	}

	u64 current_event_id()
	{
		return g_event_ctr.load();
	}

	u64 last_completed_event_id()
	{
		return g_last_completed_event.load();
	}

	void on_event_completed(u64 event_id)
	{
		g_fxo->get<vk::driver_manager_thread>().notify_completed(event_id);
		g_last_completed_event.atomic_op([event_id](u64& value)
		{
			value = std::max(event_id, value);
		});
	}

	void print_debug_markers()
	{
		for (const auto marker : g_resource_manager.gather_debug_markers())
		{
			marker->dump();
		}
	}

	static constexpr f32 size_in_GiB(u64 size)
	{
		return size / (1024.f * 1024.f * 1024.f);
	}

	void vmm_notify_memory_allocated(void* handle, u32 memory_type, u64 memory_size, vmm_allocation_pool pool)
	{
		auto key = reinterpret_cast<uptr>(handle);
		const vmm_allocation_t info = { memory_size, memory_type, pool };

		std::lock_guard lock(g_vmm_stats.mutex);

		if (const auto ins = g_vmm_stats.allocations.insert_or_assign(key, info);
			!ins.second)
		{
			rsx_log.error("Duplicate vmm entry with memory handle 0x%llx", key);
		}

		g_vmm_stats.pool_usage[pool] += memory_size;

		auto& vmm_size = g_vmm_stats.memory_usage[memory_type];
		vmm_size += memory_size;

		if (vmm_size > s_vmm_warn_threshold_size && (vmm_size - memory_size) <= s_vmm_warn_threshold_size)
		{
			rsx_log.warning("Memory type 0x%x has allocated more than %04.2fG. Currently allocated %04.2fG",
				memory_type, size_in_GiB(s_vmm_warn_threshold_size), size_in_GiB(vmm_size));
		}
	}

	void vmm_notify_memory_freed(void* handle)
	{
		std::lock_guard lock(g_vmm_stats.mutex);

		auto key = reinterpret_cast<uptr>(handle);
		if (auto found = g_vmm_stats.allocations.find(key);
			found != g_vmm_stats.allocations.end())
		{
			const auto& info = found->second;
			g_vmm_stats.memory_usage[info.type_index] -= info.size;
			g_vmm_stats.pool_usage[info.pool] -= info.size;
			g_vmm_stats.allocations.erase(found);
		}
	}

	void vmm_reset()
	{
		std::lock_guard lock(g_vmm_stats.mutex);

		g_vmm_stats.clear();
		g_event_ctr = 0;
		g_last_completed_event = 0;
#ifdef RPCS3_IOS
		g_ios_process_memory_pressure = {};
#endif
	}

	u64 vmm_get_application_memory_usage_impl(const memory_type_info& memory_type)
	{
		u64 result = 0;
		for (const auto& memory_type_index : memory_type)
		{
			auto it = g_vmm_stats.memory_usage.find(memory_type_index);
			if (it == g_vmm_stats.memory_usage.end())
			{
				continue;
			}

			result += it->second.observe();
		}

		return result;
	}

	u64 vmm_get_application_memory_usage(const memory_type_info& memory_type)
	{
		reader_lock lock(g_vmm_stats.mutex);
		return vmm_get_application_memory_usage_impl(memory_type);
	}

	u64 vmm_get_application_pool_usage_impl(vmm_allocation_pool pool)
	{
		if (auto found = g_vmm_stats.pool_usage.find(pool);
			found != g_vmm_stats.pool_usage.end())
		{
			return found->second;
		}
		return 0ull;
	}

	u64 vmm_get_application_pool_usage(vmm_allocation_pool pool)
	{
		reader_lock lock(g_vmm_stats.mutex);
		return vmm_get_application_pool_usage_impl(pool);
	}

	rsx::problem_severity vmm_determine_memory_load_severity()
	{
		reader_lock lock(g_vmm_stats.mutex);

		const auto vmm_load = get_current_mem_allocator()->get_memory_usage();
		rsx::problem_severity load_severity = rsx::problem_severity::low;

#ifdef RPCS3_IOS
		const u64 process_headroom = rpcs3::ios::available_process_memory_headroom();
		g_ios_process_memory_pressure.process_severity = rpcs3::ios::get_process_memory_pressure(
			process_headroom,
			g_ios_process_memory_pressure.process_severity);
		const auto process_severity = to_problem_severity(g_ios_process_memory_pressure.process_severity);
#else
		constexpr auto process_severity = rsx::problem_severity::low;
#endif

		// Fragmentation tuning
		if (vmm_load < 50.f && process_severity < rsx::problem_severity::moderate)
		{
			get_current_mem_allocator()->set_fastest_allocation_flags();
		}
		else if (vmm_load > 75.f || process_severity >= rsx::problem_severity::moderate)
		{
			// Avoid fragmentation if we can
			get_current_mem_allocator()->set_safest_allocation_flags();

			if (vmm_load > 95.f)
			{
				// Drivers will often crash long before returning OUT_OF_DEVICE_MEMORY errors.
				load_severity = rsx::problem_severity::fatal;
			}
			else if (vmm_load > 90.f)
			{
				load_severity = rsx::problem_severity::severe;
			}
			else if (vmm_load > 75.f)
			{
				load_severity = rsx::problem_severity::moderate;
			}

			if (vmm_load > 75.f)
			{
				// Query actual usage for comparison. Maybe we just have really fragmented memory...
				const auto mem_info = get_current_renderer()->get_memory_mapping();
				const auto local_memory_usage = vmm_get_application_memory_usage_impl(mem_info.device_local);
				const auto local_memory_available = get_available_vram_budget(
					mem_info.device_local_total_bytes,
					local_memory_usage);

				constexpr u64 _1M = 0x100000;
				const auto res_scale = rsx::get_current_renderer()->resolution_scaling_config.scale_factor();
				const auto mem_threshold_1 = static_cast<u64>(256 * res_scale * res_scale) * _1M;
				const auto mem_threshold_2 = static_cast<u64>(64 * res_scale * res_scale) * _1M;

				if (local_memory_usage < (mem_info.device_local_total_bytes / 2) ||                   // Less than 50% VRAM usage OR
					local_memory_available > mem_threshold_1)                                       // Enough to hold all required resources left
				{
					// Lower severity to avoid slowing performance too much
					load_severity = rsx::problem_severity::low;
				}
				else if (local_memory_available > mem_threshold_2)                                   // Enough to hold basic resources like textures, buffers, etc
				{
					// At least 512MB left, do not overreact
					load_severity = rsx::problem_severity::moderate;
				}

				if (load_severity >= rsx::problem_severity::moderate)
				{
#ifndef RPCS3_IOS
				// NOTE: For some reason fmt::format with a sized float followed by percentage sign causes random crashing.
				// This is a bug unrelated to this, but explains why we're going with integral percentages here.
				const auto application_memory_load = (local_memory_usage * 100) / mem_info.device_local_total_bytes;
				rsx_log.warning("Actual device memory used by internal allocations is %lluM (%llu%%)", local_memory_usage / 0x100000, application_memory_load);
				rsx_log.warning("Video memory usage is at %d%%. Will attempt to reclaim some resources.", static_cast<int>(vmm_load));
#endif
				}
			}
		}

		load_severity = std::max(load_severity, process_severity);

#ifdef RPCS3_IOS
		const auto previous_severity = std::exchange(g_ios_process_memory_pressure.combined_severity, load_severity);
		const auto now = std::chrono::steady_clock::now();
		const bool severity_changed = previous_severity != load_severity;
		const bool report_due = now >= g_ios_process_memory_pressure.next_report;

		if (load_severity >= rsx::problem_severity::moderate && (severity_changed || report_due))
		{
			const auto mem_info = get_current_renderer()->get_memory_mapping();
			const auto local_memory_usage = vmm_get_application_memory_usage_impl(mem_info.device_local);
			rsx_log.warning(
				"iOS memory pressure is %s: process headroom %llu MiB, Vulkan allocator %d%%, internal allocations %llu MiB; reclaiming caches",
				memory_severity_name(load_severity),
				process_headroom / rpcs3::ios::process_memory_mib,
				static_cast<int>(vmm_load),
				local_memory_usage / rpcs3::ios::process_memory_mib);
			g_ios_process_memory_pressure.next_report = now + std::chrono::seconds(5);
		}
		else if (severity_changed && previous_severity >= rsx::problem_severity::moderate)
		{
			rsx_log.notice(
				"iOS memory pressure recovered to %s with %llu MiB of process headroom",
				memory_severity_name(load_severity),
				process_headroom / rpcs3::ios::process_memory_mib);
		}
#endif

		return load_severity;
	}

	bool vmm_handle_memory_pressure(rsx::problem_severity severity)
	{
		if (auto vkthr = dynamic_cast<VKGSRender*>(rsx::get_current_renderer()))
		{
			return vkthr->on_vram_exhausted(severity);
		}

		return false;
	}

	void vmm_check_memory_usage()
	{
		if (const auto load_severity = vmm_determine_memory_load_severity();
			load_severity >= rsx::problem_severity::moderate)
		{
			vmm_handle_memory_pressure(load_severity);
		}
	}

	void vmm_notify_object_allocated(vmm_allocation_pool pool)
	{
		std::lock_guard lock(g_vmm_stats.mutex);

		ensure(pool >= VMM_ALLOCATION_POOL_SAMPLER);
		g_vmm_stats.pool_usage[pool]++;
	}

	void vmm_notify_object_freed(vmm_allocation_pool pool)
	{
		std::lock_guard lock(g_vmm_stats.mutex);

		ensure(pool >= VMM_ALLOCATION_POOL_SAMPLER);
		g_vmm_stats.pool_usage[pool]--;
	}
}
