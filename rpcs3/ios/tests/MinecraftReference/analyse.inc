void draw_command_processor::analyse_inputs_interleaved(vertex_input_layout& result, const vertex_program_metadata_t& vp_metadata)
	{
		const rsx_state& state = *REGS(m_ctx);
		const u32 input_mask = state.vertex_attrib_input_mask() & vp_metadata.referenced_inputs_mask;

		result.clear();
		result.attribute_mask = static_cast<u16>(input_mask);

		if (state.current_draw_clause.command == rsx::draw_command::inlined_array)
		{
			interleaved_range_info& info = *result.alloc_interleaved_block();
			info.interleaved = true;

			for (u8 index = 0; index < rsx::limits::vertex_count; ++index)
			{
				auto& vinfo = state.vertex_arrays_info[index];
				result.attribute_placement[index] = attribute_buffer_placement::none;

				if (vinfo.size() > 0)
				{
					// Stride must be updated even if the stream is disabled
					info.attribute_stride += rsx::get_vertex_type_size_on_host(vinfo.type(), vinfo.size());
					info.locations.push_back({ index, false, 1 });

					if (input_mask & (1u << index))
					{
						result.attribute_placement[index] = attribute_buffer_placement::transient;
					}
				}
				else if (state.register_vertex_info[index].size > 0 && input_mask & (1u << index))
				{
					// Reads from register
					result.referenced_registers.push_back(index);
					result.attribute_placement[index] = attribute_buffer_placement::transient;
				}
			}

			if (info.attribute_stride)
			{
				// At least one array feed must be enabled for vertex input
				result.interleaved_blocks.push_back(&info);
			}

			return;
		}

		const u32 frequency_divider_mask = REGS(m_ctx)->frequency_divider_operation_mask();
		result.interleaved_blocks.reserve(16);
		result.referenced_registers.reserve(16);

		for (auto [ref_mask, index] = std::tuple{ input_mask, u8(0) }; ref_mask; ++index, ref_mask >>= 1)
		{
			ensure(index < rsx::limits::vertex_count);

			if (!(ref_mask & 1u))
			{
				// Nothing to do, uninitialized
				continue;
			}

			// Always reset attribute placement by default
			result.attribute_placement[index] = attribute_buffer_placement::none;

			// Check for interleaving
			if (REGS(m_ctx)->current_draw_clause.is_immediate_draw &&
				REGS(m_ctx)->current_draw_clause.command != rsx::draw_command::indexed)
			{
				// NOTE: In immediate rendering mode, all vertex setup is ignored
				// Observed with GT5, immediate render bypasses array pointers completely, even falling back to fixed-function register defaults
				if (m_vertex_push_buffers[index].vertex_count > 1)
				{
					// Ensure consistent number of vertices per attribute.
					m_vertex_push_buffers[index].pad_to(m_vertex_push_buffers[0].vertex_count, false);

					// Read temp buffer (register array)
					std::pair<u8, u32> volatile_range_info = std::make_pair(index, static_cast<u32>(m_vertex_push_buffers[index].data.size() * sizeof(u32)));
					result.volatile_blocks.push_back(volatile_range_info);
					result.attribute_placement[index] = attribute_buffer_placement::transient;
				}
				else if (state.register_vertex_info[index].size > 0)
				{
					// Reads from register
					result.referenced_registers.push_back(index);
					result.attribute_placement[index] = attribute_buffer_placement::transient;
				}

				// Fall back to the default register value if no source is specified via register
				continue;
			}

			const auto& info = state.vertex_arrays_info[index];
			if (!info.size())
			{
				if (state.register_vertex_info[index].size > 0)
				{
					// Reads from register
					result.referenced_registers.push_back(index);
					result.attribute_placement[index] = attribute_buffer_placement::transient;
					continue;
				}
			}
			else
			{
				result.attribute_placement[index] = attribute_buffer_placement::persistent;
				const u32 base_address = info.offset() & 0x7fffffff;
				bool alloc_new_block = true;
				bool modulo = !!(frequency_divider_mask & (1 << index));

				for (auto& block : result.interleaved_blocks)
				{
					if (block->single_vertex)
					{
						// Single vertex definition, continue
						continue;
					}

					if (block->attribute_stride != info.stride())
					{
						// Stride does not match, continue
						continue;
					}

					if (base_address > block->base_offset)
					{
						const u32 diff = base_address - block->base_offset;
						if (diff > info.stride())
						{
							// Not interleaved, continue
							continue;
						}
					}
					else
					{
						const u32 diff = block->base_offset - base_address;
						if (diff > info.stride())
						{
							// Not interleaved, continue
							continue;
						}

						// Matches, and this address is lower than existing
						block->base_offset = base_address;
					}

					alloc_new_block = false;
					block->locations.push_back({ index, modulo, info.frequency() });
					block->interleaved = true;
					break;
				}

				if (alloc_new_block)
				{
					interleaved_range_info& block = *result.alloc_interleaved_block();
					block.base_offset = base_address;
					block.attribute_stride = info.stride();
					block.memory_location = info.offset() >> 31;
					block.locations.reserve(16);
					block.locations.push_back({ index, modulo, info.frequency() });

					if (block.attribute_stride == 0)
					{
						block.single_vertex = true;
						block.attribute_stride = rsx::get_vertex_type_size_on_host(info.type(), info.size());
					}

					result.interleaved_blocks.push_back(&block);
				}
			}
		}

		for (auto& info : result.interleaved_blocks)
		{
			// Calculate real data address to be used during upload
			info->real_offset_address = rsx::get_address(rsx::get_vertex_offset_from_base(state.vertex_data_base_offset(), info->base_offset), info->memory_location);
		}
	}
