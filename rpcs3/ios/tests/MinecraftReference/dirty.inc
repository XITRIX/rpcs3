inline void notify_descriptor_slot_updated(u32 slot, const T& data)
			{
				m_descriptors_dirty[slot] = true;
				m_descriptor_slots[slot] = data;
				m_any_descriptors_dirty = true;
			}
