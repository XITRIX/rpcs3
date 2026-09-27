const auto next_cache_line = [&](int current)
				{
					if (m_cache_line_count == 8)
					{
						// Original RPCS3's 1 KiB cache traversal.
						return (std::countr_zero<u32>(std::rotl<u8>(static_cast<u8>(to_fetch), 0 - current - 1)) + current + 1) % 8;
					}
					// ARMSX3's 4 KiB cache traversal.
					return (std::countr_zero<u32>(std::rotl<u32>(to_fetch, 0 - current - 1)) + current + 1) % 32;
				}