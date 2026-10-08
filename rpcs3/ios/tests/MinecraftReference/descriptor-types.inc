struct VkDescriptorImageInfoEx : public VkDescriptorImageInfo
	{
		u64 resourceId = 0ull;

		VkDescriptorImageInfoEx() = default;
		VkDescriptorImageInfoEx(const vk::image_view& view, const vk::sampler& sampler, VkImageLayout layout);
		VkDescriptorImageInfoEx(const vk::image_view& view, const vk::sampler& sampler);
		VkDescriptorImageInfoEx(const vk::image_view& view, VkSampler sampler);
		VkDescriptorImageInfoEx(const vk::image_view& view);
	};
struct VkDescriptorBufferInfoEx : public VkDescriptorBufferInfo
	{
		u64 resourceId = 0ull;

		VkDescriptorBufferInfoEx() = default;
		VkDescriptorBufferInfoEx(const vk::buffer& buffer, u64 offset, u64 range);
	};
struct VkDescriptorBufferViewEx
	{
		u64 resourceId = 0ull;
		VkBufferView view = VK_NULL_HANDLE;

		VkDescriptorBufferViewEx() = default;
		VkDescriptorBufferViewEx(const vk::buffer_view& view);
	};