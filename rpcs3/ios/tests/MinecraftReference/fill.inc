void draw_command_processor::fill_vertex_layout_state(
		const vertex_input_layout& layout,
		const vertex_program_metadata_t& vp_metadata,
		u32 first_vertex,
		u32 vertex_count,
		s32* buffer,
		u32 persistent_offset_base,
		u32 volatile_offset_base) const
	{
		std::array<s32, 16> offset_in_block = {};
		u32 volatile_offset = volatile_offset_base;
		u32 persistent_offset = persistent_offset_base;

		// NOTE: Order is important! Transient ayout is always push_buffers followed by register data
		if (REGS(m_ctx)->current_draw_clause.is_immediate_draw)
		{
			for (const auto& info : layout.volatile_blocks)
			{
				offset_in_block[info.first] = volatile_offset;
				volatile_offset += info.second;
			}
		}

		for (u8 index : layout.referenced_registers)
		{
			offset_in_block[index] = volatile_offset;
			volatile_offset += 16;
		}

		if (REGS(m_ctx)->current_draw_clause.command == rsx::draw_command::inlined_array)
		{
			const auto& block = layout.interleaved_blocks[0];
			u32 inline_data_offset = volatile_offset;
			for (const auto& attrib : block->locations)
			{
				auto& info = REGS(m_ctx)->vertex_arrays_info[attrib.index];

				offset_in_block[attrib.index] = inline_data_offset;
				inline_data_offset += rsx::get_vertex_type_size_on_host(info.type(), info.size());
			}
		}
		else
		{
			for (const auto& block : layout.interleaved_blocks)
			{
				for (const auto& attrib : block->locations)
				{
					const u32 local_address = (REGS(m_ctx)->vertex_arrays_info[attrib.index].offset() & 0x7fffffff);
					offset_in_block[attrib.index] = persistent_offset + (local_address - block->base_offset);
				}

				const auto range = block->calculate_required_range(first_vertex, vertex_count);
				persistent_offset += block->attribute_stride * range.second;
			}
		}

		// Fill the data
		// Each descriptor field is 64 bits wide
		// [0-8] attribute stride
		// [8-24] attribute divisor
		// [24-27] attribute type
		// [27-30] attribute size
		// [30-31] reserved
		// [31-60] starting offset
		// [60-21] swap bytes flag
		// [61-22] volatile flag
		// [62-63] modulo enable flag

		const s32 default_frequency_mask = (1 << 8);
		const s32 swap_storage_mask = (1 << 29);
		const s32 volatile_storage_mask = (1 << 30);
		const s32 modulo_op_frequency_mask = smin;

		const u32 modulo_mask = REGS(m_ctx)->frequency_divider_operation_mask();
		const auto max_index = (first_vertex + vertex_count) - 1;

		for (u16 ref_mask = vp_metadata.referenced_inputs_mask, index = 0; ref_mask; ++index, ref_mask >>= 1)
		{
			if (!(ref_mask & 1u))
			{
				// Unused input, ignore this
				continue;
			}

			if (layout.attribute_placement[index] == attribute_buffer_placement::none)
			{
				static constexpr u64 zero = 0;
				std::memcpy(buffer + index * 2, &zero, sizeof(zero));
				continue;
			}

			rsx::vertex_base_type type = {};
			s32 size = 0;
			s32 attrib0 = 0;
			s32 attrib1 = 0;

			if (layout.attribute_placement[index] == attribute_buffer_placement::transient)
			{
				if (REGS(m_ctx)->current_draw_clause.command == rsx::draw_command::inlined_array)
				{
					const auto& info = REGS(m_ctx)->vertex_arrays_info[index];

					if (!info.size())
					{
						// Register
						const auto& reginfo = REGS(m_ctx)->register_vertex_info[index];
						type = reginfo.type;
						size = reginfo.size;

						attrib0 = rsx::get_vertex_type_size_on_host(type, size);
					}
					else
					{
						// Array
						type = info.type();
						size = info.size();

						attrib0 = layout.interleaved_blocks[0]->attribute_stride | default_frequency_mask;
					}
				}
				else
				{
					// Data is either from an immediate render or register input
					// Immediate data overrides register input

					if (REGS(m_ctx)->current_draw_clause.is_immediate_draw &&
						m_vertex_push_buffers[index].vertex_count > 1)
					{
						// Push buffer
						const auto& info = m_vertex_push_buffers[index];
						type = info.type;
						size = info.size;

						attrib0 = rsx::get_vertex_type_size_on_host(type, size) | default_frequency_mask;
					}
					else
					{
						// Register
						const auto& info = REGS(m_ctx)->register_vertex_info[index];
						type = info.type;
						size = info.size;

						attrib0 = rsx::get_vertex_type_size_on_host(type, size);
					}
				}

				attrib1 |= volatile_storage_mask;
			}
			else
			{
				auto& info = REGS(m_ctx)->vertex_arrays_info[index];
				type = info.type();
				size = info.size();

				auto stride = info.stride();
				attrib0 = stride;

				if (stride > 0) // when stride is 0, input is not an array but a single element
				{
					const u32 frequency = info.frequency();
					switch (frequency)
					{
					case 0:
					case 1:
					{
						attrib0 |= default_frequency_mask;
						break;
					}
					default:
					{
						if (modulo_mask & (1 << index))
						{
							if (max_index >= frequency)
							{
								// Only set modulo mask if a modulo op is actually necessary!
								// This requires that the uploaded range for this attr = [0, freq-1]
								// Ignoring modulo op if the rendered range does not wrap allows for range optimization
								attrib0 |= (frequency << 8);
								attrib1 |= modulo_op_frequency_mask;
							}
							else
							{
								attrib0 |= default_frequency_mask;
							}
						}
						else
						{
							// Division
							attrib0 |= (frequency << 8);
						}
						break;
					}
					}
				}
			} // end attribute placement check

			// Special compressed 4 components into one 4-byte value. Decoded as one value.
			if (type == rsx::vertex_base_type::cmp)
			{
				size = 1;
			}

			// All data is passed in in PS3-native order (BE) so swap flag should be set
			attrib1 |= swap_storage_mask;
			attrib0 |= (static_cast<s32>(type) << 24);
			attrib0 |= (size << 27);
			attrib1 |= offset_in_block[index];

			buffer[index * 2 + 0] = attrib0;
			buffer[index * 2 + 1] = attrib1;
		}
	}
