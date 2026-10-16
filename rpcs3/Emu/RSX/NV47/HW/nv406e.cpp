#include "stdafx.h"
#include "nv406e.h"
#include "nv47_sync.hpp"

#include "Emu/RSX/RSXThread.h"
#include "Emu/system_config.h"

#include "context_accessors.define.h"

namespace rsx
{
	namespace nv406e
	{
		void set_reference(context* ctx, u32 /*reg*/, u32 arg)
		{
			RSX(ctx)->sync();

			// Write ref+get (get will be written again with the same value at command end)
			auto& dma = *vm::_ptr<RsxDmaControl>(RSX(ctx)->dma_address);
			//dma.get.store(RSX(ctx)->fifo_ctrl->get_pos() + 4);
			dma.ref.store(arg);
		}

		void semaphore_acquire(context* ctx, u32 /*reg*/, u32 arg)
		{
			RSX(ctx)->sync_point_request.release(true);
			const u32 addr = get_address(REGS(ctx)->semaphore_offset_406e(), REGS(ctx)->semaphore_context_dma_406e());

			// Syncronization point, may be associated with memory changes without actually changing addresses
			RSX(ctx)->m_graphics_state |= rsx::pipeline_state::fragment_program_needs_rehash;

			// Ensure atomic seq-cst memory ordering for FIFO GET update
			atomic_fence_seq_cst();

			const auto& sema = vm::_ref<RsxSemaphore>(addr);
			const auto& atomic_sema = vm::_ref<atomic_t<RsxSemaphore>>(addr);

			RSX(ctx)->last_sema_cmd = RSX(ctx)->fifo_ctrl->get_pos() - 4;
			RSX(ctx)->last_sema_addr = addr;

			if (sema == arg)
			{
				// Flip semaphore doesnt need wake-up delay
				if (addr != RSX(ctx)->label_addr + 0x10)
				{
					//RSX(ctx)->flush_fifo();
					RSX(ctx)->fifo_wake_delay(2);
				}

				return;
			}
			else
			{
				//RSX(ctx)->flush_fifo();
			}

			u64 start = get_system_time();
			u64 last_check_val = start;
			bool timeout_reported = false;
#ifdef RPCS3_IOS
			u32 wait_spins = 0;
#endif

			while (sema != arg)
			{
				if (RSX(ctx)->test_stopped())
				{
					RSX(ctx)->state += cpu_flag::again;
					return;
				}

				if (const auto tdr = static_cast<u64>(g_cfg.video.driver_recovery_timeout); tdr && !timeout_reported)
				{
					const u64 current = get_system_time();

					if (current - last_check_val > 20'000)
					{
						// Suspicious amnount of time has passed
						// External pause such as debuggers' pause or operating system sleep may have taken place
						// Ignore it
						start += current - last_check_val;
					}

					last_check_val = current;

					if ((current - start) > tdr)
					{
						rsx_log.error("nv406e::semaphore_acquire has timed out. semaphore_address=0x%X, expected=0x%X, observed=0x%X", addr, arg, atomic_sema.load());
#ifdef RPCS3_IOS
						// A slow guest producer is not permission to consume its
						// unfinished commands/resources. Keep servicing the backend
						// and native Stop/Pause until the semaphore is satisfied.
						// Report once instead of poisoning shader caches after timeout.
						timeout_reported = true;
#else
						break;
#endif
					}
				}

				if (RSX(ctx)->external_interrupt_lock ||
					(RSX(ctx)->state & (cpu_flag::dbg_global_pause + cpu_flag::exit)) == cpu_flag::dbg_global_pause)
				{
					RSX(ctx)->cpu_wait({});
					continue;
				}

				RSX(ctx)->on_semaphore_acquire_wait();

#ifdef RPCS3_IOS
				// ARM event waits keep the host thread runnable. After a short
				// spin, let the guest producer use the core. Service work and
				// stop checks still run on every bounded retry; ordinary guest
				// semaphore stores need not issue an emulator notification.
				if (wait_spins < 32)
				{
					++wait_spins;
					utils::spin_on_cacheline_once(atomic_sema, sema, 100);
				}
				else if (atomic_sema.load() != arg)
				{
					thread_ctrl::wait_for(50);
				}
#else
				// Wait until the value changes or until 100us pass.
				utils::spin_on_cacheline_once(atomic_sema, sema, 100);
#endif
			}

			RSX(ctx)->fifo_wake_delay();
			RSX(ctx)->performance_counters.idle_time += (get_system_time() - start);
		}

		void semaphore_release(context* ctx, u32 reg, u32 arg)
		{
			const u32 offset = REGS(ctx)->semaphore_offset_406e();

			if (offset % 4)
			{
				rsx_log.warning("NV406E semaphore release is using unaligned semaphore, ignoring. (offset=0x%x)", offset);
				return;
			}

			const u32 ctxt = REGS(ctx)->semaphore_context_dma_406e();

			// By avoiding doing this on flip's semaphore release
			// We allow last gcm's registers reset to occur in case of a crash
			if (const bool is_flip_sema = (offset == 0x10 && ctxt == CELL_GCM_CONTEXT_DMA_SEMAPHORE_R);
				!is_flip_sema)
			{
				RSX(ctx)->sync_point_request.release(true);
			}

			const u32 addr = get_address(offset, ctxt);

			// TODO: Check if possible to write on reservations
			if (RSX(ctx)->label_addr >> 28 != addr >> 28)
			{
				rsx_log.error("NV406E semaphore unexpected address. Please report to the developers. (offset=0x%x, addr=0x%x)", offset, addr);
				RSX(ctx)->recover_fifo();
				return;
			}

			if (addr == RSX(ctx)->device_addr + 0x30 && !arg)
			{
				// HW flip synchronization related, 1 is not written without display queue command (TODO: make it behave as real hw)
				arg = 1;
			}

			util::write_gcm_label<false, true>(ctx, reg, addr, arg);
		}
	}
}
