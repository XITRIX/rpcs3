#include "../RPCS3IOSBootProgress.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

using u8 = uint8_t;
using u32 = uint32_t;
using s32 = int32_t;
using u64 = uint64_t;
template <typename T, typename U> T narrow(U value) { return static_cast<T>(value); }
#define ensure(condition) assert(condition)

// Only filesystem and Vulkan boundaries are substituted. The runner extracts
// the unchanged production path selection, restore, save and teardown methods.
namespace fs
{
using bytes = std::vector<u8>;
std::map<std::string, std::shared_ptr<bytes>> files;
std::vector<std::string> reads, writes;
constexpr int read = 1;
std::string get_cache_dir() { return "/cache/"; }
std::string get_parent_dir(const std::string& path) { return path.substr(0, path.rfind('/')); }
bool create_path(const std::string&) { return true; }
struct file
{
	std::shared_ptr<bytes> data;
	u64 offset = 0;
	bool open(const std::string& path, int)
	{
		reads.push_back(path);
		const auto found = files.find(path);
		if (found == files.end()) return false;
		data = found->second;
		return true;
	}
	explicit operator bool() const { return bool(data); }
	u64 size() const { return data->size(); }
	u64 read(void* dest, u64 count)
	{
		count = std::min(count, size() - offset);
		std::memcpy(dest, data->data() + offset, count);
		offset += count;
		return count;
	}
	u64 write(const void* source, u64 count)
	{
		const auto first = static_cast<const u8*>(source);
		data->insert(data->end(), first, first + count);
		return count;
	}
};
struct pending_file
{
	std::string path;
	fs::file file{std::make_shared<bytes>()};
	explicit pending_file(std::string destination) : path(std::move(destination)) {}
	bool commit()
	{
		writes.push_back(path);
		files[path] = file.data;
		return true;
	}
};
}

struct config { struct { bool disable_on_disk_shader_cache = false; } video; } g_cfg;
namespace rpcs3::cache
{
std::string active_root;
std::string get_ppu_cache() { return active_root; }
}
struct logger
{
	template <typename... T> void notice(const char*, T&&...) {}
	template <typename... T> void warning(const char*, T&&...) {}
} rsx_log;

using VkPipelineCache = u64;
using VkResult = int;
constexpr VkResult VK_SUCCESS = 0;
constexpr VkResult VK_INCOMPLETE = 5;
constexpr u64 VK_NULL_HANDLE = 0;
constexpr u32 VK_UUID_SIZE = 16;
constexpr int VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO = 1;
struct VkPipelineCacheCreateInfo
{
	int sType;
	size_t initialDataSize = 0;
	const void* pInitialData = nullptr;
};
std::vector<std::vector<u8>> driver_inputs;
std::vector<u8> driver_output;
bool reject_next_restore = false;
VkResult vkCreatePipelineCache(u64, const VkPipelineCacheCreateInfo* info, void*, VkPipelineCache* handle)
{
	std::vector<u8> input(info->initialDataSize);
	if (!input.empty()) std::memcpy(input.data(), info->pInitialData, input.size());
	driver_inputs.push_back(std::move(input));
	if (reject_next_restore && info->initialDataSize)
	{
		reject_next_restore = false;
		return -1;
	}
	*handle = 1;
	return VK_SUCCESS;
}
VkResult vkGetPipelineCacheData(u64, VkPipelineCache, size_t* size, void* output)
{
	*size = driver_output.size();
	if (output) std::memcpy(output, driver_output.data(), *size);
	return VK_SUCCESS;
}
void vkDestroyPipelineCache(u64, VkPipelineCache, void*) {}

namespace vk
{
struct physical_device
{
	struct { u32 vendorID = 1, deviceID = 2; u8 pipelineCacheUUID[VK_UUID_SIZE]{}; } props;
};
struct render_device
{
	u64 dev = 1;
	physical_device* pgpu;
	VkPipelineCache m_pipeline_cache = VK_NULL_HANDLE;
	std::string m_pipeline_cache_path;
	void create_pipeline_cache();
	void save_pipeline_cache() const;
	void save_and_destroy_pipeline_cache();
};
}

#include "VulkanPipelineCacheUnderTest.h"

int main()
{
	const std::string a = "/cache/A/ppu-executable1/";
	const std::string b = "/cache/B/ppu-executable2/";
	const std::string suffix = "shaders_cache/vk_pipeline_cache.bin";
	const std::string legacy = "/cache/vk_pipeline_cache.bin";
	fs::files[legacy] = std::make_shared<fs::bytes>(100, 99);
	vk::physical_device gpu;
	vk::render_device device{.pgpu = &gpu};

	// A cold boot ignores the all-games legacy cache, then saves to its original
	// executable even if the active title changes before teardown.
	rpcs3::cache::active_root = a;
	device.create_pipeline_cache();
	assert(driver_inputs.back().empty());
	driver_output = {1, 2, 3};
	rpcs3::cache::active_root = b;
	device.save_and_destroy_pipeline_cache();
	assert(fs::writes.back() == a + suffix);
	assert(device.m_pipeline_cache_path.empty());

	device.create_pipeline_cache();
	assert(driver_inputs.back().empty());
	driver_output = {4, 5, 6};
	device.save_and_destroy_pipeline_cache();
	assert(fs::writes.back() == b + suffix);

	// A warm boot restores only that executable's payload.
	rpcs3::cache::active_root = a;
	device.create_pipeline_cache();
	assert((driver_inputs.back() == std::vector<u8>{1, 2, 3}));
	device.save_and_destroy_pipeline_cache();

	// Disabled persistence and unidentified/guestless sessions do not read or
	// overwrite any disk cache, but still get a usable in-memory Vulkan cache.
	const auto read_count = fs::reads.size(), write_count = fs::writes.size();
	g_cfg.video.disable_on_disk_shader_cache = true;
	device.create_pipeline_cache();
	assert(device.m_pipeline_cache && driver_inputs.back().empty());
	device.save_and_destroy_pipeline_cache();
	g_cfg.video.disable_on_disk_shader_cache = false;
	rpcs3::cache::active_root.clear();
	device.create_pipeline_cache();
	device.save_and_destroy_pipeline_cache();
	assert(fs::reads.size() == read_count && fs::writes.size() == write_count);

	// A changed executable does not import its predecessor's driver data.
	rpcs3::cache::active_root = "/cache/A/ppu-executable-updated/";
	device.create_pipeline_cache();
	assert(driver_inputs.back().empty());
	device.save_and_destroy_pipeline_cache();

	// Preserve driver rejection fallback and clear scoped startup activity.
	rpcs3::cache::active_root = a;
	reject_next_restore = true;
	const auto calls = driver_inputs.size();
	device.create_pipeline_cache();
	assert(driver_inputs.size() == calls + 2);
	assert(!driver_inputs[calls].empty() && driver_inputs[calls + 1].empty());
	assert(rpcs3::ios::boot_stages().snapshot().empty());
	device.save_and_destroy_pipeline_cache();
	assert(std::find(fs::reads.begin(), fs::reads.end(), legacy) == fs::reads.end());
	assert(std::find(fs::writes.begin(), fs::writes.end(), legacy) == fs::writes.end());
	assert(*fs::files[legacy] == fs::bytes(100, 99));
}
