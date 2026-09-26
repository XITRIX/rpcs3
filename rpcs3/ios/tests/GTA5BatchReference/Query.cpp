inline bool query_pool_manager::poke_query(query_slot_info& query, u32 index, VkQueryResultFlags flags)
	{
		// Query is ready if:
		// 1. Any sample has been determined to have passed the Z test
		// 2. The backend has fully processed the query and found no hits

		u32 result[2] = { 0, 0 };
		switch (const auto error = vkGetQueryPoolResults(*owner, *query.pool, index, 1, 8, result, 8, flags | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT))
		{
		case VK_SUCCESS:
		{
			if (result[0])
			{
				query.any_passed = true;
				query.ready = true;
				query.data = result[0];
				return true;
			}
			else if (result[1])
			{
				query.any_passed = false;
				query.ready = true;
				query.data = 0;
				return true;
			}

			return false;
		}
		case VK_NOT_READY:
		{
			query.any_passed = !!result[0];
			query.ready = query.any_passed && !!(flags & VK_QUERY_RESULT_PARTIAL_BIT);
			query.data = result[0];
			return query.ready;
		}
		default:
			die_with_error(error);
			return false;
		}
	}
bool query_pool_manager::check_query_status(u32 index)
	{
		return poke_query(query_slot_status[index], index, result_flags);
	}
u32 query_pool_manager::get_query_result(u32 index)
	{
		// Check for cached result
		auto& query_info = query_slot_status[index];

		if (!query_info.ready)
		{
			poke_query(query_info, index, result_flags);

			while (!query_info.ready)
			{
				utils::pause();
				poke_query(query_info, index, result_flags);
			}
		}

		return query_info.data;
	}
void query_pool_manager::free_query(vk::command_buffer&/*cmd*/, u32 index)
	{
		// Release reference and discard
		auto& query = query_slot_status[index];

		ensure(query.active);
		query.pool->release();

		if (!query.pool->has_refs())
		{
			// No more refs held, remove if in discard pile
			run_pool_cleanup();
		}

		query = {};
		m_available_slots.push_back(index);
	}