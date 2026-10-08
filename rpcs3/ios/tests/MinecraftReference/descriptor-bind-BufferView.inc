void program::bind_uniform(const VkDescriptorBufferViewEx& buffer_view, u32 set_id, u32 binding_point)
		{
			if (m_sets[set_id].m_descriptor_slots[binding_point] == buffer_view)
			{
				return;
			}

			m_sets[set_id].notify_descriptor_slot_updated(binding_point, buffer_view);
		}