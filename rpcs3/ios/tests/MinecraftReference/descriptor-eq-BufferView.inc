bool operator == (const descriptor_slot_t& a, const VkDescriptorBufferViewEx& b)
		{
			const auto ptr = std::get_if<VkDescriptorBufferViewEx>(&a);
			return !!ptr && ptr->resourceId == b.resourceId;
		}