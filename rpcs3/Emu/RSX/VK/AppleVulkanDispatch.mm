#include "stdafx.h"
#define RPCS3_APPLE_VULKAN_IMPLEMENTATION
#include "VulkanAPI.h"
#include "AppleVulkanDispatch.h"
#include "util/logs.hpp"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <TargetConditionals.h>
#include <dlfcn.h>
#include <filesystem>
#include <vector>
#include <cstring>

// The stock backend stays statically linked and works on the original OS floor.
// Only renderer call sites use these aliases; the driver retains its own symbols.
#define APPLE_VK(name) PFN_##name rpcs3_##name = &name;
#include "AppleVulkanFunctions.inc"
#undef APPLE_VK

namespace vk::apple
{
	namespace
	{
		void* loader = nullptr; // Process lifetime: never unload driver code.
		bool active_kosmickrisp = false;
		struct dispatch
		{
#define APPLE_VK(name) PFN_##name name = nullptr;
#include "AppleVulkanFunctions.inc"
#undef APPLE_VK
		} kosmic_dispatch;

		backend_availability probe()
		{
#if TARGET_OS_SIMULATOR
			return {false, "Metal 4 is unavailable in the Simulator."};
#else
			@autoreleasepool
			{
				// These are runtime gates, independent of the application's deployment target.
				if (@available(macOS 26.0, iOS 26.0, visionOS 26.0, *))
				{
					id<MTLDevice> device = MTLCreateSystemDefaultDevice();
					const bool supported = device && [device supportsFamily:MTLGPUFamilyMetal4];
					[device release];
					if (!supported)
						return {false, "This GPU does not support Metal 4."};
				}
				else
					return {false, "KosmicKrisp requires an operating system with Metal 4."};
			}
#endif
			Dl_info image{};
			if (!dladdr(reinterpret_cast<const void*>(&select_backend), &image) || !image.dli_fname)
				return {false, "Cannot locate the bundled graphics runtime."};
			const auto frameworks = std::filesystem::path(image.dli_fname).parent_path();
			const auto manifest = frameworks / "KosmicKrisp.framework/kosmickrisp_icd.json";
			const auto library = frameworks / "VulkanLoader.framework/VulkanLoader";
			if (!std::filesystem::is_regular_file(manifest) || !std::filesystem::is_regular_file(library))
				return {false, "The KosmicKrisp runtime is not included in this build."};
			// Only our signed, bundled ICD is discoverable. This does not affect static MVK.
			setenv("VK_DRIVER_FILES", manifest.c_str(), 1);
			setenv("VK_ICD_FILENAMES", manifest.c_str(), 1);
			setenv("VK_LOADER_LAYERS_DISABLE", "~all~", 1);
			loader = dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
			if (!loader)
				return {false, std::string("Cannot load the graphics runtime: ") + dlerror()};
#define APPLE_VK(name)                                                         \
	kosmic_dispatch.name = reinterpret_cast<PFN_##name>(dlsym(loader, #name)); \
	if (!kosmic_dispatch.name)                                                 \
		return {false, "The graphics runtime is missing " #name "."};
#include "AppleVulkanFunctions.inc"
#undef APPLE_VK
			const auto& d = kosmic_dispatch;
			uint32_t count = 0;
			if (d.vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS)
				return {false, "Cannot query the graphics runtime."};
			std::vector<VkExtensionProperties> extensions(count);
			if (d.vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()) != VK_SUCCESS)
				return {false, "Cannot query graphics extensions."};
			const auto has = [&](const char* name)
			{
				return std::ranges::any_of(extensions, [&](const auto& e)
					{
						return std::strcmp(e.extensionName, name) == 0;
					});
			};
			if (!has(VK_EXT_METAL_SURFACE_EXTENSION_NAME) || !has(VK_KHR_SURFACE_EXTENSION_NAME))
				return {false, "The graphics driver cannot present to a Metal surface."};
			VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
			app.pApplicationName = "RPCS3 graphics availability";
			app.apiVersion = VK_API_VERSION_1_2;
			VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
			info.pApplicationInfo = &app;
			VkInstance instance = VK_NULL_HANDLE;
			if (d.vkCreateInstance(&info, nullptr, &instance) != VK_SUCCESS)
				return {false, "The graphics driver could not create a Vulkan instance."};
			bool found = false;
			if (d.vkEnumeratePhysicalDevices(instance, &count, nullptr) == VK_SUCCESS && count)
			{
				std::vector<VkPhysicalDevice> devices(count);
				if (d.vkEnumeratePhysicalDevices(instance, &count, devices.data()) == VK_SUCCESS)
					for (const auto device : devices)
					{
						VkPhysicalDeviceDriverProperties driver{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
						VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &driver};
						d.vkGetPhysicalDeviceProperties2(device, &properties);
						if (driver.driverID != VK_DRIVER_ID_MESA_KOSMICKRISP || properties.properties.apiVersion < VK_API_VERSION_1_2)
							continue;
						uint32_t n = 0;
						if (d.vkEnumerateDeviceExtensionProperties(device, nullptr, &n, nullptr) != VK_SUCCESS)
							continue;
						std::vector<VkExtensionProperties> device_extensions(n);
						if (d.vkEnumerateDeviceExtensionProperties(device, nullptr, &n, device_extensions.data()) != VK_SUCCESS)
							continue;
						if (!std::ranges::any_of(device_extensions, [](const auto& e)
								{
									return std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
								}))
							continue;
						uint32_t queue_count = 0;
						d.vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, nullptr);
						std::vector<VkQueueFamilyProperties> queues(queue_count);
						d.vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, queues.data());
						for (uint32_t family = 0; family < queue_count; ++family)
						{
							if (!queues[family].queueCount ||
								(queues[family].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) != (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
								continue;
							const float priority = 1.0f;
							VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
							queue.queueFamilyIndex = family;
							queue.queueCount = 1;
							queue.pQueuePriorities = &priority;
							const char* swapchain = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
							VkDeviceCreateInfo create{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
							create.queueCreateInfoCount = 1;
							create.pQueueCreateInfos = &queue;
							create.enabledExtensionCount = 1;
							create.ppEnabledExtensionNames = &swapchain;
							VkDevice logical = VK_NULL_HANDLE;
							if (d.vkCreateDevice(device, &create, nullptr, &logical) == VK_SUCCESS)
							{
								d.vkDestroyDevice(logical, nullptr);
								found = true;
							}
							break;
						}
					}
			}
			d.vkDestroyInstance(instance, nullptr);
			return {found, found ? "" : "No compatible KosmicKrisp Vulkan device was found."};
		}
	} // namespace

	const backend_availability& kosmickrisp_availability()
	{
		static const backend_availability result = []
		{
			try
			{
				return probe();
			}
			catch (const std::exception& e)
			{
				return backend_availability{false, e.what()};
			}
			catch (...)
			{
				return backend_availability{false, "Graphics availability check failed."};
			}
		}();
		return result;
	}

	void select_backend(const std::string& requested)
	{
		const bool use_kosmic = requested == "KosmicKrisp" && kosmickrisp_availability().available;
		if (requested == "KosmicKrisp" && !use_kosmic)
			rsx_log.warning("KosmicKrisp unavailable: %s. Using MoltenVK for this boot; saved settings are preserved.", kosmickrisp_availability().reason);
#define APPLE_VK(name) rpcs3_##name = use_kosmic ? kosmic_dispatch.name : &name;
#include "AppleVulkanFunctions.inc"
#undef APPLE_VK
		active_kosmickrisp = use_kosmic;
		rsx_log.notice("Apple Vulkan backend: %s", use_kosmic ? "KosmicKrisp" : "MoltenVK");
	}

	bool using_kosmickrisp()
	{
		return active_kosmickrisp;
	}
} // namespace vk::apple
