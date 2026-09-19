#include "Utilities/address_range.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <shared_mutex>

namespace
{
void require(bool value, const char* message)
{
	if (!value)
	{
		std::fprintf(stderr, "%s\n", message);
		std::exit(EXIT_FAILURE);
	}
}

// Models the VM range-lock handoff required before taking an RSX lock.
bool vm_range_held = false;
struct vm_range_lock
{
	u32 releases = 0;
	void release(u64)
	{
		vm_range_held = false;
		++releases;
	}
};
}

struct { struct { bool rsx_accurate_res_access = true; } core; } g_cfg;
void* get_current_cpu_thread() { return nullptr; }

namespace rsx
{
namespace constants { constexpr u32 local_mem_base = 0xc0000000; }

struct rsx_iomap_table
{
	static constexpr u32 c_lock_stride = 8192;
	std::array<std::shared_mutex, 8> mutexes;

	template <bool Full, uint Stride>
	bool lock(u32 addr, u32 length, void*)
	{
		// This is the production IO-map contract for single-byte transfers.
		if (length <= 1) return false;
		require(!vm_range_held, "RSX lock acquired before releasing the VM range lock");
		for (u32 page = addr / c_lock_stride; page <= (addr + length - 1) / c_lock_stride; page += Stride)
		{
			if constexpr (Full) mutexes.at(page).lock();
			else mutexes.at(page).lock_shared();
		}
		return true;
	}

	template <bool Full, uint Stride>
	void unlock(u32 addr, u32 length)
	{
		for (u32 page = addr / c_lock_stride; page <= (addr + length - 1) / c_lock_stride; page += Stride)
		{
			if constexpr (Full) mutexes.at(page).unlock();
			else mutexes.at(page).unlock_shared();
		}
	}

	bool renderer_can_read(u32 page)
	{
		// Use another host thread: recursively locking shared_mutex on the
		// owning thread would not test the actual reader/writer exclusion.
		return std::async(std::launch::async, [this, page]
		{
			const bool acquired = mutexes.at(page).try_lock();
			if (acquired) mutexes.at(page).unlock();
			return acquired;
		}).get();
	}
};

struct renderer { rsx_iomap_table iomap_table; } instance;
renderer* get_current_renderer() { return &instance; }
}

#include "RSXReservationLockUnderTest.h"

int main()
{
	auto& map = rsx::instance.iomap_table;
	constexpr u32 page = rsx::rsx_iomap_table::c_lock_stride;
	vm_range_lock range;
	auto* range_ptr = &range;

	// A large/non-inline or local-memory list element drops the current
	// reservation; the next inline main-memory PUT must be protected again.
	for (u32 destination : {64u, 2 * page})
	{
		rsx::reservation_lock<false, 1> transfer(0, 128, true);
		require(!map.renderer_can_read(0), "initial PUT was not protected");
		transfer.unlock();
		require(map.renderer_can_read(0), "temporary release retained the old lock");
		vm_range_held = true;
		transfer.update_if_enabled(destination, 128, range_ptr);
		require(!map.renderer_can_read(destination / page), "PUT after a temporary release is unprotected");
		require(!vm_range_held, "VM range was not released during reacquisition");
	}

	// A one-byte entry does not acquire an IO-map lock. It must not disable
	// reservation protection for every remaining element of the DMA list.
	{
		rsx::reservation_lock<false, 1> transfer(0, 128, true);
		transfer.update_if_enabled(page, 1);
		require(map.renderer_can_read(0) && map.renderer_can_read(1), "single-byte transfer retained a lock");
		transfer.update_if_enabled(page + 64, 128);
		require(!map.renderer_can_read(1), "PUT after a one-byte list element is unprotected");
	}
	{
		rsx::reservation_lock<false, 1> transfer(page, 1, true);
		transfer.update_if_enabled(page + 64, 128);
		require(!map.renderer_can_read(1), "initial one-byte transfer disabled later protection");
	}

	// Preserve the fast path when an existing reservation covers the next
	// entry, and cover every page when an entry crosses a lock boundary.
	{
		rsx::reservation_lock<false, 1> transfer(0, 128, true);
		vm_range_held = true;
		const u32 releases = range.releases;
		transfer.update_if_enabled(64, 128, range_ptr);
		require(vm_range_held && range.releases == releases, "covered entry unnecessarily released the VM range");
		transfer.update_if_enabled(2 * page - 64, 128, range_ptr);
		require(map.renderer_can_read(0), "moving a reservation retained the old page");
		require(!map.renderer_can_read(1) && !map.renderer_can_read(2), "cross-page PUT is only partially protected");
	}

	// Explicitly disabled protection must stay disabled after updates.
	{
		rsx::reservation_lock<false, 1> transfer(0, 128, false);
		const u32 releases = range.releases;
		transfer.update_if_enabled(page - 64, 128, range_ptr);
		require(range.releases == releases, "disabled lock changed the VM range");
		require(map.renderer_can_read(0) && map.renderer_can_read(1), "disabled reservation was acquired");
	}
	for (u32 i = 0; i < map.mutexes.size(); ++i)
		require(map.renderer_can_read(i), "reservation leaked past its scope");
	std::puts("SPU DMA list reservation handoff, byte entries, page boundaries and disabled policy passed");
}
