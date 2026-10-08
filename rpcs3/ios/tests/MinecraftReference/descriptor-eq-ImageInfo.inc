bool operator == (const descriptor_slot_t& a, const VkDescriptorImageInfoEx& b)
		{
			const auto ptr = std::get_if<VkDescriptorImageInfoEx>(&a);
			return !!ptr &&
				ptr->resourceId == b.resourceId &&
				ptr->imageView == b.imageView &&
				ptr->sampler == b.sampler &&
				ptr->imageLayout == b.imageLayout;
		}