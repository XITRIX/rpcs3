// Production instance creation/debug setup against a controllable Vulkan driver.
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_metal.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using u32 = uint32_t;
using s32 = int32_t;
constexpr auto kMVKMoltenVKDriverLayerName = "MoltenVK";
constexpr int MVK_CONFIG_FAST_MATH_NEVER = 0;
constexpr int MVK_CONFIG_FAST_MATH_ON_DEMAND = 2;
struct boolean_setting
{
	bool value = false;
	operator bool() const { return value; }
	bool get() const { return value; }
};
struct config
{
	struct
	{
		boolean_setting debug_output, disable_msl_fast_math, renderdoc_compatiblity;
		struct { std::string to_string() const { return "MoltenVK"; } } apple_graphics_backend;
	} video;
} g_cfg;
struct logger
{
	int fatals = 0, warnings = 0;
	template <typename... T> void fatal(const char*, T...) { ++fatals; }
	template <typename... T> void warning(const char*, T...) { ++warnings; }
	template <typename... T> void error(const char*, T...) {}
} rsx_log;

static bool debug_extension = true, create_proc = true, destroy_proc = true;
static VkResult driver_result = VK_SUCCESS;
static int create_calls = 0, callback_calls = 0, proc_queries = 0;
static u32 requested_layers = 0;
static bool requested_debug_extension = false;
static void check(bool value, const char* message)
{
	if (!value) { std::fprintf(stderr, "%s\n", message); std::abort(); }
}
#define CHECK_RESULT(expr) check((expr) == VK_SUCCESS, "Vulkan callback failed")

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(
	const VkInstanceCreateInfo* info, const VkAllocationCallbacks*, VkInstance* result)
{
	++create_calls;
	requested_layers = info->enabledLayerCount;
	requested_debug_extension = false;
	for (u32 i = 0; i < info->enabledExtensionCount; ++i)
		requested_debug_extension |= std::strcmp(info->ppEnabledExtensionNames[i], VK_EXT_DEBUG_REPORT_EXTENSION_NAME) == 0;
#ifdef RPCS3_IOS
	// Reproduce the bundled ICD's rejection of a nonexistent validation layer.
	if (info->enabledLayerCount) return VK_ERROR_LAYER_NOT_PRESENT;
#endif
	if (driver_result == VK_SUCCESS) *result = reinterpret_cast<VkInstance>(uintptr_t{1});
	return driver_result;
}
static VKAPI_ATTR VkResult VKAPI_CALL create_callback(
	VkInstance, const VkDebugReportCallbackCreateInfoEXT*, const VkAllocationCallbacks*, VkDebugReportCallbackEXT* out)
{
	check(requested_debug_extension, "callback used without enabled extension");
	++callback_calls;
	*out = reinterpret_cast<VkDebugReportCallbackEXT>(uintptr_t{2});
	return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL destroy_callback(VkInstance, VkDebugReportCallbackEXT, const VkAllocationCallbacks*) {}
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance, const char* name)
{
	++proc_queries;
	if (debug_extension && create_proc && std::strcmp(name, "vkCreateDebugReportCallbackEXT") == 0)
		return reinterpret_cast<PFN_vkVoidFunction>(&create_callback);
	if (debug_extension && destroy_proc && std::strcmp(name, "vkDestroyDebugReportCallbackEXT") == 0)
		return reinterpret_cast<PFN_vkVoidFunction>(&destroy_callback);
	return nullptr;
}
namespace vk
{
	static VKAPI_ATTR VkBool32 VKAPI_CALL dbgFunc(VkDebugReportFlagsEXT, VkDebugReportObjectTypeEXT,
		uint64_t, size_t, int32_t, const char*, const char*, void*) { return VK_FALSE; }
	namespace apple { void select_backend(const std::string&) {} }
	class supported_extensions
	{
	public:
		enum enumeration_class { instance };
		explicit supported_extensions(enumeration_class) {}
		bool is_supported(std::string_view name) const
		{
			return name != VK_EXT_DEBUG_REPORT_EXTENSION_NAME || debug_extension;
		}
	};
	class instance
	{
	public:
		VkInstance m_instance = VK_NULL_HANDLE;
		VkDebugReportCallbackEXT m_debugger = VK_NULL_HANDLE;
		PFN_vkCreateDebugReportCallbackEXT _vkCreateDebugReportCallback = nullptr;
		PFN_vkDestroyDebugReportCallbackEXT _vkDestroyDebugReportCallback = nullptr;
		bool extensions_loaded = false;
		bool create(const char*, bool = false);
		void enable_debugging();
	};
#include "VulkanInstanceUnderTest.inc"
}

static int cases = 0;
static void run(bool debug, bool extension, bool create_available, bool destroy_available,
	bool fast = false, VkResult result = VK_SUCCESS)
{
	g_cfg.video.debug_output.value = debug;
	debug_extension = extension;
	create_proc = create_available;
	destroy_proc = destroy_available;
	driver_result = result;
	create_calls = callback_calls = proc_queries = 0;
	rsx_log = {};
	vk::instance instance;
	const bool success = instance.create("diagnostic regression", fast);
	check(success == (result == VK_SUCCESS), "instance startup failed");
	check(create_calls == 1, "unexpected instance retry");
	check(g_cfg.video.debug_output.value == debug, "saved debug setting changed");
#ifdef RPCS3_IOS
	check(requested_layers == 0, "bundled Apple runtime requested external layers");
#else
	check(requested_layers == static_cast<u32>(debug && !fast), "desktop validation request changed");
#endif
	if (!success)
	{
		check(rsx_log.fatals == 1 && callback_calls == 0, "real driver failure suppressed");
	}
	else if (!fast)
	{
		instance.enable_debugging();
		check(callback_calls == static_cast<int>(debug && extension && create_available && destroy_available),
			"available driver diagnostics lost or unavailable callback invoked");
		check(proc_queries == (debug ? 2 : 0), "disabled diagnostics queried callbacks");
		check(rsx_log.fatals == 0, "optional diagnostics caused a fatal");
	}
	++cases;
}
int main(int argc, char**)
{
	if (argc > 1)
	{
		run(true, false, false, false);
	}
	else
	{
		// First case reproduces the nine observed layer-not-present failures.
		run(true, true, true, true);
		for (bool debug : {false, true})
			for (bool extension : {false, true})
				for (bool create_available : {false, true})
					for (bool destroy_available : {false, true})
				{
					run(debug, extension, create_available, destroy_available);
					run(debug, extension, create_available, destroy_available, true);
					run(debug, extension, create_available, destroy_available, false, VK_ERROR_INITIALIZATION_FAILED);
				}
	}
	std::printf("Vulkan instance diagnostics: %d cases passed\n", cases);
}
