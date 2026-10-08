std::pair<u32, u32> interleaved_range_info::calculate_required_range(u32 first, u32 count)
	{
		if (vertex_range.second)
		{
			// Cached result
			return vertex_range;
		}

		if (single_vertex)
		{
			return { 0, 1 };
		}

		const u32 max_index = (first + count) - 1;
		u32 _max_index = 0;
		u32 _min_index = first;

		u32 frequencies[rsx::limits::vertex_count];
		u32 freq_count = rsx::method_registers.current_draw_clause.command == rsx::draw_command::indexed ? 0 : u32{umax};
		u32 max_result_by_division = 0; // Guaranteed maximum

		for (const auto &attrib : locations)
		{
			if (attrib.frequency <= 1) [[likely]]
			{
				freq_count = umax;
				_max_index = max_index;
			}
			else
			{
				if (attrib.modulo)
				{
					if (max_index >= attrib.frequency)
					{
						// Actually uses the modulo operator
						_min_index = 0;
						_max_index = std::max<u32>(_max_index, attrib.frequency - 1);

						if (max_result_by_division < _max_index)
						{
							if (freq_count != umax)
							{
								if (std::find(frequencies, frequencies + freq_count, attrib.frequency) == frequencies + freq_count)
								{
									frequencies[freq_count++] = attrib.frequency;
								}
							}
						}
					}
					else
					{
						// Same as having no modulo
						_max_index = max_index;
						freq_count = umax;
					}
				}
				else
				{
					// Division operator
					_min_index = std::min(_min_index, first / attrib.frequency);
					_max_index = std::max<u32>(_max_index, utils::aligned_div(max_index, attrib.frequency));

					if (freq_count > 0 && freq_count != umax)
					{
						const u32 max = utils::aligned_div(max_index, attrib.frequency);
						max_result_by_division = std::max<u32>(max_result_by_division, max);

						// Discard lower frequencies because it has been proven that there are indices higher than them
						const usz discard_cnt = frequencies + freq_count - std::remove_if(frequencies, frequencies + freq_count, [&max_result_by_division](u32 freq)
						{
							return freq <= max_result_by_division;
						});

						freq_count -= static_cast<u32>(discard_cnt);
					}
				}
			}
		}

		while (freq_count > 0 && freq_count != umax)
		{
			const rsx::index_array_type index_type = rsx::method_registers.current_draw_clause.is_immediate_draw ?
				rsx::index_array_type::u32 :
				rsx::method_registers.index_type();

			const u32 index_size = index_type == rsx::index_array_type::u32 ? 4 : 2;

			const auto render = rsx::get_current_renderer();

			// If we can access a bit a more memory than required - do it
			// The alternative would be re-iterating again over all of them
			if (get_location(real_offset_address) == CELL_GCM_LOCATION_LOCAL)
			{
				if (utils::add_saturate<u32>(real_offset_address - rsx::constants::local_mem_base, (_max_index + 1) * attribute_stride) <= render->local_mem_size)
				{
					break;
				}
			}
			else if (real_offset_address % 0x100000 + (_max_index + 1) * attribute_stride <= 0x100000)//(vm::check_addr(real_offset_address, vm::page_readable, (_max_index + 1) * attribute_stride))
			{
				break;
			}

			_max_index = 0;

			const auto re_evaluate = [&] <typename T> (const std::byte* ptr, T)
			{
				const u64 restart = rsx::method_registers.restart_index_enabled() ? rsx::method_registers.restart_index() : u64{umax};

				for (u32 _index = first; _index < first + count; _index++)
				{
					const auto value = read_from_ptr_unsafe<be_t<T>>(ptr, _index * sizeof(T));

					if (value == restart)
					{
						continue;
					}

					for (u32 freq_it = 0; freq_it < freq_count; freq_it++)
					{
						const auto res = value % frequencies[freq_it];

						if (res > _max_index)
						{
							_max_index = res;
						}
					}
				}
			};

			const auto element_push_buffer = render->draw_processor()->element_push_buffer();
			if (index_size == 4)
			{
				if (!element_push_buffer.empty()) [[unlikely]]
				{
					// Indices provided via immediate mode
					re_evaluate(reinterpret_cast<const std::byte*>(element_push_buffer.data()), u32{});
				}
				else
				{
					const u32 address = (0 - index_size) & get_address(rsx::method_registers.index_array_address(), rsx::method_registers.index_array_location());
					re_evaluate(vm::get_super_ptr<std::byte>(address), u32{});
				}
			}
			else
			{
				if (!element_push_buffer.empty()) [[unlikely]]
				{
					// Indices provided via immediate mode
					re_evaluate(reinterpret_cast<const std::byte*>(element_push_buffer.data()), u16{});
				}
				else
				{
					const u32 address = (0 - index_size) & get_address(rsx::method_registers.index_array_address(), rsx::method_registers.index_array_location());
					re_evaluate(vm::get_super_ptr<std::byte>(address), u16{});
				}
			}

			break;
		}

		ensure(_max_index >= _min_index);
		vertex_range = { _min_index, (_max_index - _min_index) + 1 };
		return vertex_range;
	}
