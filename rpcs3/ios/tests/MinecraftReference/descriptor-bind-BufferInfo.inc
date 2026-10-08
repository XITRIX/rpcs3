void program::bind_uniform(const VkDescriptorBufferInfoEx &buffer_descriptor, u32 set_id, u32 binding_point)
		{
			if (m_sets[set_id].m_descriptor_slots[binding_point] == buffer_descriptor)
			{
				return;
			}

			m_sets[set_id].notify_descriptor_slot_updated(binding_point, buffer_descriptor);
		}