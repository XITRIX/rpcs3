void fragment_program_texture_config::masked_transfer(void* dst, const void* src, u16 mask)
	{
		// Try to optimize for the very common case (first 4 slots used)
		switch (mask)
		{
		case 0:
			return;
		case 1:
			std::memcpy(dst, src, sizeof(TIU_slot)); return;
		case 3:
			std::memcpy(dst, src, sizeof(TIU_slot) * 2); return;
		case 7:
			std::memcpy(dst, src, sizeof(TIU_slot) * 3); return;
		case 15:
			std::memcpy(dst, src, sizeof(TIU_slot) * 4); return;
		default:
			break;
		}

		const auto start = std::countr_zero(mask);
		const auto end = 16 - std::countl_zero(mask);
		const auto mem_offset = (start * sizeof(TIU_slot));
		const auto mem_size = (end - start) * sizeof(TIU_slot);
		std::memcpy(static_cast<u8*>(dst) + mem_offset, reinterpret_cast<const u8*>(src) + mem_offset, mem_size);
	}
