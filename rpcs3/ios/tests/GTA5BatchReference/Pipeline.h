// Check if another submission completed in the mean time
			if (const auto I = m_storage.find(key); I != m_storage.end())
			{
				m_cache_miss_flag = (I->second == __null_pipeline_handle);
				return { I->second.get(), &vertex_program, &fragment_program };
			}

			// Insert a placeholder if the key still doesn't exist to avoid re-linking of the same pipeline
			m_storage[key] = std::move(__null_pipeline_handle);
		}