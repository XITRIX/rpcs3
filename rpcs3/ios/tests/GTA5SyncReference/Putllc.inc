		if (raddr != addr)
		{
			return false;
		}

		const auto& to_write = _ref<spu_rdata_t>(args.lsa & 0x3ff80);
		// In relaxed reservation mode, unchanged local data succeeds regardless
		// of the reservation timestamp. No guest-memory access or RSX lock is
		// needed for this existing no-write case.
		const bool accurate = static_cast<bool>(g_cfg.core.spu_accurate_reservations);
		const bool unchanged = !accurate && cmp_rdata(to_write, rdata);
		if (unchanged)
		{
			raddr = 0;
			return true;
		}

		auto& res = vm::reservation_acquire(addr);

		// TODO: Limit scope!!
		rsx::reservation_lock rsx_lock(addr, 128);

		if (rtime != res)
		{
			return false;
		}

		if (accurate && cmp_rdata(to_write, rdata))
		{
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

		const bool relaxed_spurs = !accurate && addr - spurs_addr <= 0x80;
		const usz diff16_pos = relaxed_spurs ? usz{umax} : scan16_rdata(to_write, rdata);

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
			if (relaxed_spurs)
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