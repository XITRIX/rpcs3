vk::sampler* get_sampler(const vk::render_device& dev, vk::sampler* previous,
			VkSamplerAddressMode clamp_u, VkSamplerAddressMode clamp_v, VkSamplerAddressMode clamp_w,
			VkBool32 unnormalized_coordinates, float mipLodBias, float max_anisotropy, float min_lod, float max_lod,
			VkFilter min_filter, VkFilter mag_filter, VkSamplerMipmapMode mipmap_mode, const vk::border_color_t& border_color,
			VkBool32 depth_compare = VK_FALSE, VkCompareOp depth_compare_mode = VK_COMPARE_OP_NEVER)
		{
			const auto key = m_sampler_pool.compute_storage_key(
				clamp_u, clamp_v, clamp_w,
				unnormalized_coordinates, mipLodBias, max_anisotropy, min_lod, max_lod,
				min_filter, mag_filter, mipmap_mode, border_color,
				depth_compare, depth_compare_mode);

			if (previous)
			{
				auto as_cached_object = static_cast<cached_sampler_object_t*>(previous);
				ensure(as_cached_object->has_refs());
				as_cached_object->release();
			}

			if (const auto found = m_sampler_pool.find(key))
			{
				found->add_ref();
				return found;
			}

			auto result = std::make_unique<cached_sampler_object_t>(
				dev, clamp_u, clamp_v, clamp_w, unnormalized_coordinates,
				mipLodBias, max_anisotropy, min_lod, max_lod,
				min_filter, mag_filter, mipmap_mode, border_color,
				depth_compare, depth_compare_mode);

			auto ret = m_sampler_pool.emplace(key, result);
			ret->add_ref();
			return ret;
		}