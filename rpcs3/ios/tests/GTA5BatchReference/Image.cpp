viewable_image* viewable_image::clone()
	{
		// Destructive cloning. The clone grabs the GPU objects owned by this instance.
		// This instance can be rebuilt in-place by calling create_impl() which will create a duplicate now owned by this.
		auto result = new viewable_image();
		result->m_device = this->m_device;
		result->info = this->info;
		result->value = this->value;
		result->memory = std::move(this->memory);
		result->views = std::move(this->views);
		this->value = VK_NULL_HANDLE;
		return result;
	}
image_view* viewable_image::get_view(const rsx::texture_channel_remap_t& remap, VkImageAspectFlags mask)
	{
		u32 remap_encoding = remap.encoded;
		if (remap_encoding == VK_REMAP_IDENTITY)
		{
			if (native_component_map.a == VK_COMPONENT_SWIZZLE_A &&
				native_component_map.r == VK_COMPONENT_SWIZZLE_R &&
				native_component_map.g == VK_COMPONENT_SWIZZLE_G &&
				native_component_map.b == VK_COMPONENT_SWIZZLE_B)
			{
				remap_encoding = RSX_TEXTURE_REMAP_IDENTITY;
			}
		}

		const u64 storage_key = remap_encoding | (static_cast<u64>(mask) << 32);
		auto found = views.find(storage_key);
		if (found != views.end())
		{
			ensure(found->second->info.subresourceRange.aspectMask & mask);
			return found->second.get();
		}

		VkComponentMapping real_mapping;
		switch (remap_encoding)
		{
		case VK_REMAP_IDENTITY:
			real_mapping = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
			break;
		case RSX_TEXTURE_REMAP_IDENTITY:
			real_mapping = native_component_map;
			break;
		default:
			real_mapping = vk::apply_swizzle_remap
			(
				{ native_component_map.a, native_component_map.r, native_component_map.g, native_component_map.b },
				remap
			);
			break;
		}

		const VkImageSubresourceRange range = { aspect() & mask, 0, info.mipLevels, 0, info.arrayLayers };
		ensure(range.aspectMask);

		auto view = std::make_unique<vk::image_view>(*g_render_device, this, format(), VK_IMAGE_VIEW_TYPE_MAX_ENUM, real_mapping, range);
		auto result = view.get();
		views.emplace(storage_key, std::move(view));
		return result;
	}
void viewable_image::set_native_component_layout(VkComponentMapping new_layout)
	{
		if (new_layout.r != native_component_map.r ||
			new_layout.g != native_component_map.g ||
			new_layout.b != native_component_map.b ||
			new_layout.a != native_component_map.a)
		{
			native_component_map = new_layout;

			// Safely discard existing views
			auto gc = vk::get_resource_manager();
			for (auto& p : views)
			{
				gc->dispose(p.second);
			}
			views.clear();
		}
	}