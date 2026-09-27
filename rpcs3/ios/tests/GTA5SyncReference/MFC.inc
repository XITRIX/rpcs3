auto get_exec_mask = [&size = mfc_size]
	{
		// Get commands' execution mask
		// Mask bits are always set when mfc_transfers_shuffling is 0
		return static_cast<u16>((0 - (1u << std::min<u32>(g_cfg.core.mfc_transfers_shuffling, size))) | utils::get_tsc());
	}
bool spu_thread::check_mfc_interrupts(u32 next_pc)
{
	if (ch_events.load().count && std::exchange(interrupts_enabled, false))
	{
		srr0 = next_pc;

		// Test for BR/BRA instructions (they are equivalent at zero pc)
		const u32 br = _ref<u32>(0);
		pc = (br & 0xfd80007f) == 0x30000000 ? (br >> 5) & 0x3fffc : 0;
		return true;
	}

	return false;
}