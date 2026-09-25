#include "metal_event.h"
#include "ios/IOSGPUEventWait.h"
#include "Emu/Cell/timers.hpp"
#include "util/asm.hpp"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#import <Metal/Metal.h>
#pragma clang diagnostic pop

namespace vk
{
void* export_metal_event(VkDevice device, VkEvent event)
{
    const auto export_objects = reinterpret_cast<PFN_vkExportMetalObjectsEXT>(
        vkGetDeviceProcAddr(device, "vkExportMetalObjectsEXT"));
    if (!export_objects)
    {
        return nullptr;
    }

    VkExportMetalSharedEventInfoEXT shared_event{
        .sType = VK_STRUCTURE_TYPE_EXPORT_METAL_SHARED_EVENT_INFO_EXT,
        .pNext = nullptr,
        .semaphore = VK_NULL_HANDLE,
        .event = event,
        .mtlSharedEvent = nullptr,
    };
    VkExportMetalObjectsInfoEXT objects{
        .sType = VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT,
        .pNext = &shared_event,
    };
    export_objects(device, &objects);
    return shared_event.mtlSharedEvent;
}

VkResult wait_for_metal_event(void* metal_event, VkDevice device, VkEvent event, u64 timeout_us)
{
    const auto shared_event = static_cast<id<MTLSharedEvent>>(metal_event);
    const auto result = rpcs3::ios::wait_for_gpu_event(VK_EVENT_RESET, VK_TIMEOUT, timeout_us,
        [&] { return shared_event.signaledValue; },
        [&] { return vkGetEventStatus(device, event); },
        [] { return get_system_time(); },
        [&](u64 value, u64 milliseconds) { [shared_event waitUntilSignaledValue:value timeoutMS:milliseconds]; },
        [] { utils::pause(); });
    return result == VK_EVENT_SET ? VK_SUCCESS : result;
}
}
