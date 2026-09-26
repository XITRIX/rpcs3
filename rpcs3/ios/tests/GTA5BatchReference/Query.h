void free_queries(vk::command_buffer& cmd, T& list)
		{
			for (const auto index : list)
			{
				free_query(cmd, index);
			}
		}