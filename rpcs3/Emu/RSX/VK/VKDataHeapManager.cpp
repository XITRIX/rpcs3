#include "stdafx.h"
#include "VKDataHeapManager.h"

#include "vkutils/data_heap.h"
#include <mutex>
#include <unordered_set>

#ifdef RPCS3_IOS
#include "ios/RPCS3IOSExperimentalPolicy.h"
#endif

namespace vk::data_heap_manager
{
	std::unordered_set<vk::data_heap*> g_managed_heaps;

	rsx::simple_array<vk::data_heap*> to_list()
	{
		rsx::simple_array<vk::data_heap*> result;
		result.resize(::size32(g_managed_heaps));
		std::copy(g_managed_heaps.begin(), g_managed_heaps.end(), result.begin());
		return result;
	}

	void register_ring_buffer(vk::data_heap& heap)
	{
		g_managed_heaps.insert(&heap);
	}

	void register_ring_buffers(std::initializer_list<std::reference_wrapper<vk::data_heap>> heaps)
	{
		for (auto&& heap : heaps)
		{
			register_ring_buffer(heap);
		}
	}

	managed_heap_snapshot_t get_heap_snapshot()
	{
		managed_heap_snapshot_t result{};
		for (auto& heap : g_managed_heaps)
		{
			result[heap] = heap->get_current_put_pos_minus_one();
		}
		return result;
	}

	bool use_command_buffer_reclamation() noexcept
	{
#ifdef RPCS3_IOS
		return rpcs3::ios::get_experimental_policy().vulkan_command_buffer_reclamation;
#else
		return false;
#endif
	}

	static atomic_t<u64> g_snapshot_counter{0};
	static u64 g_last_applied_snapshot = 0;
	static std::mutex g_snapshot_restore_mutex;

	u64 next_snapshot_id()
	{
		return ++g_snapshot_counter;
	}

	void restore_snapshot(const managed_heap_snapshot_t& snapshot, u64 id)
	{
		// Applying the id and ring pointers is one ordered operation. The
		// mutex matters if fence completions are observed by different
		// callers: an atomic check/store alone can still let an older
		// snapshot finish after a newer one.
		std::lock_guard lock(g_snapshot_restore_mutex);
		if (id && id <= g_last_applied_snapshot)
		{
			return;
		}

		if (id)
		{
			g_last_applied_snapshot = id;
		}

		for (auto& heap : g_managed_heaps)
		{
			const auto found = snapshot.find(heap);
			if (found == snapshot.end())
			{
				continue;
			}

			heap->set_get_pos(found->second);
			heap->notify();
		}
	}

	void reset_heap_allocations()
	{
		for (auto& heap : g_managed_heaps)
		{
			heap->reset_allocation_stats();
		}
	}

	void reset()
	{
		for (auto& heap : g_managed_heaps)
		{
			heap->destroy();
		}

		g_managed_heaps.clear();
	}
}
