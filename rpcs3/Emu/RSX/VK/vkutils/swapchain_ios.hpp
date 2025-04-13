#pragma once

#include "swapchain_core.h"

namespace vk
{
#if TARGET_OS_IOS
	using swapchain_IOS = native_swapchain_base;
	using swapchain_NATIVE = swapchain_IOS;

	[[maybe_unused]] static VkSurfaceKHR make_WSI_surface(VkInstance vk_instance, display_handle_t window_handle, WSI_config* /*config*/)
	{
		VkSurfaceKHR result = VK_NULL_HANDLE;
		VkIOSSurfaceCreateInfoMVK createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_IOS_SURFACE_CREATE_INFO_MVK;
		createInfo.pView = window_handle;

		CHECK_RESULT(VK_GET_SYMBOL(vkCreateIOSSurfaceMVK)(vk_instance, &createInfo, NULL, &result));
		return result;
	}
#endif
} // namespace vk
