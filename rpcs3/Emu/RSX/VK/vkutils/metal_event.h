#pragma once

#include "../VulkanAPI.h"

namespace vk
{
// Borrowed from the VkEvent; valid until that event is destroyed.
void* export_metal_event(VkDevice device, VkEvent event);
VkResult wait_for_metal_event(void* metal_event, VkDevice device, VkEvent event, u64 timeout_us);
}
