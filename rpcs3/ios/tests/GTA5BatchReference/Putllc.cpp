bool spu_thread::do_putllc(const spu_mfc_cmd& args)
{
	perf_meter<"PUTLLC-"_u64> perf0(nullptr);
	perf_meter<"PUTLLC+"_u64> perf1 = perf0;

	// Store conditionally
	const u32 addr = args.eal & -128;

	if ([&]()
	{
		perf_meter<"PUTLLC."_u64> perf2 = perf0;

		if (raddr != addr)
		{
			return false;
		}

		const auto& to_write = _ref<spu_rdata_t>(args.lsa & 0x3ff80);
		auto& res = vm::reservation_acquire(addr);

		// TODO: Limit scope!!
		rsx::reservation_lock rsx_lock(addr, 128);

		if (rtime != res)
		{
			if (!g_cfg.core.spu_accurate_reservations && cmp_rdata(to_write, rdata))
			{
				raddr = 0;
				return true;
			}

			return false;
		}

		if (cmp_rdata(to_write, rdata))
		{
			if (!g_cfg.core.spu_accurate_reservations)
			{
				raddr = 0;
				return true;
			}

			// Writeback of unchanged data. Only check memory change
			// For the comparison, load twice for atomicity
			if (cmp_rdata(rdata, vm::_ref<spu_rdata_t>(addr)) && res == rtime && cmp_rdata(rdata, vm::_ref<spu_rdata_t>(addr)) && res.compare_and_swap_test(rtime, rtime + 128))
			{
				raddr = 0; // Disable notification
				return true;
			}

			return false;
		}

		static const auto cast_as = [](void* ptr, usz pos){ return reinterpret_cast<u128*>(ptr) + pos; };
		static const auto cast_as_const = [](const void* ptr, usz pos){ return reinterpret_cast<const u128*>(ptr) + pos; };

		const usz diff16_pos = scan16_rdata(to_write, rdata);

		auto [_oldd, _ok] = res.fetch_op([&](u64& r)
		{
			if ((r & -128) != rtime || (r & 127))
			{
				return false;
			}

			r += vm::rsrv_unique_lock;
			return true;
		});

		if (!_ok)
		{
			// Already locked or updated: give up
			return false;
		}

		if (!g_cfg.core.spu_accurate_reservations)
		{
			if (addr - spurs_addr <= 0x80)
			{
				mov_rdata(*vm::_ptr<spu_rdata_t>(addr), to_write);
				res += 64;
				return true;
			}
		}
		else
		{
			utils::trigger_write_page_fault(vm::base(addr));
		}

		auto& super_data = *vm::get_super_ptr<spu_rdata_t>(addr);
		const bool success = [&]()
		{
			if (!g_cfg.core.spu_accurate_reservations && diff16_pos != umax)
			{
				vm::range_lock<128>(range_lock, addr, 128);
				const bool ok = cmp_rdata(rdata, super_data) && atomic_storage<u128>::compare_exchange(*cast_as(super_data, diff16_pos), *cast_as(rdata, diff16_pos), *cast_as_const(to_write, diff16_pos));
				range_lock->release(0);
				return ok;
			}

			// Full lock (heavyweight)
			// TODO: vm::check_addr
			vm::writer_lock lock(addr, range_lock);

			if (cmp_rdata(rdata, super_data))
			{
				if (diff16_pos != umax)
				{
					// Do it with CMPXCHG16B if possible, this allows to improve accuracy whenever "RSX Accurate Reservations" is off
					if (atomic_storage<u128>::compare_exchange(*cast_as(super_data, diff16_pos), *cast_as(rdata, diff16_pos), *cast_as_const(to_write, diff16_pos)))
					{
						return true;
					}
				}
				else
				{
					mov_rdata(super_data, to_write);
					return true;
				}
			}

			return false;
		}();

		res += success ? 64 : 0 - 64;
		return success;
	}())
	{
		if (raddr)
		{
			if (raddr != spurs_addr || pc != 0x11e4)
			{
				vm::reservation_notifier_notify(addr, rtime);
			}
			else
			{
				const u32 thread_bit_mask = (1u << index);
				constexpr usz SPU_IDLE = 0x73;

				const bool switched_from_running_to_idle = (static_cast<u8>(rdata[SPU_IDLE]) & thread_bit_mask) == 0 && (_ref<u8>(0x100 + SPU_IDLE) & thread_bit_mask) != 0;

				if (switched_from_running_to_idle)
				{
					vm::reservation_notifier_notify(addr, rtime);
				}
			}

			raddr = 0;
		}

		perf0.reset();
		return true;
	}
	else
	{
		if (raddr)
		{
			// Last check for event before we clear the reservation
			if (~ch_events.load().events & SPU_EVENT_LR)
			{
				if (raddr == addr)
				{
					set_events(SPU_EVENT_LR);
				}
				else
				{
					get_events(SPU_EVENT_LR);
				}
			}
		}

		if (!vm::check_addr(addr, vm::page_writable))
		{
			utils::trigger_write_page_fault(vm::base(addr));
		}

		raddr = 0;
		perf1.reset();
		return false;
	}
}
