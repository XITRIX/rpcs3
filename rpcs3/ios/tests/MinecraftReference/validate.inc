bool validate() const
		{
			// Criteria: At least one array stream has to be defined to feed vertex positions
			// This stream cannot be a const register as the vertices cannot create a zero-area primitive
			if (!interleaved_blocks.empty() && interleaved_blocks[0]->attribute_stride != 0)
				return true;

			if (!volatile_blocks.empty())
				return true;

			for (u16 ref_mask = attribute_mask, index = 0; ref_mask; ++index, ref_mask >>= 1)
			{
				if (!(ref_mask & 1))
				{
					// Disabled
					continue;
				}

				switch (attribute_placement[index])
				{
				case attribute_buffer_placement::transient:
				{
					// Ignore register reference
					if (std::find(referenced_registers.begin(), referenced_registers.end(), index) != referenced_registers.end())
						continue;

					// The source is inline array or immediate draw push buffer
					return true;
				}
				case attribute_buffer_placement::persistent:
				{
					return true;
				}
				case attribute_buffer_placement::none:
				{
					continue;
				}
				default:
				{
					fmt::throw_exception("Unreachable");
				}
				}
			}

			return false;
		}
