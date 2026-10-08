bool operator == (const descriptor_slot_t& a, const VkDescriptorBufferInfoEx& b)
		{
			const auto ptr = std::get_if<VkDescriptorBufferInfoEx>(&a);
			return !!ptr &&
				ptr->resourceId == b.resourceId &&
				ptr->buffer == b.buffer &&
				ptr->offset == b.offset &&
				ptr->range == b.range;
		}