#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using usz = size_t;
using VkFormat = u32;
using VkImageType = u32;
using VkImageCreateFlags = u32;
using VkImageUsageFlags = u32;
using VkSharingMode = u32;
using shared_mutex = std::shared_mutex;

static std::atomic<unsigned> checks{0};
template <typename T> static void ensure(const T& value)
{
    ++checks;
    if (!value) throw std::runtime_error("Image-pool ownership/accounting assertion failed");
}
template <typename T> u32 size32(const T& values) { return static_cast<u32>(values.size()); }
struct reader_lock
{
    shared_mutex& mutex;
    bool exclusive = false;
    explicit reader_lock(shared_mutex& m) : mutex(m) { mutex.lock_shared(); }
    void upgrade() { mutex.unlock_shared(); mutex.lock(); exclusive = true; }
    ~reader_lock() { if (exclusive) mutex.unlock(); else mutex.unlock_shared(); }
};
namespace rsx { enum class problem_severity { low, moderate, severe, fatal }; }
struct { void warning(const char*) {} } rsx_log;
namespace vk
{
struct memory_block { u64 bytes = 4096; u64 size() const { return bytes; } };
struct viewable_image
{
    // Match the reported arm64 image usage offset, verified from product DWARF.
    std::array<u8, 0xb8> padding{};
    struct { u32 usage = 3, imageType = 0, flags = 0, sharingMode = 0; } info;
    u32 current_layout{}, current_queue_family{}, identity{};
    std::unique_ptr<memory_block> memory = std::make_unique<memory_block>();
    u32 format() const { return 1; }
    u16 width() const { return 2; }
    u16 height() const { return 2; }
    u16 depth() const { return 1; }
    u16 mipmaps() const { return 1; }
};
static_assert(offsetof(viewable_image, info) == 0xb8);
constexpr u32 VK_IMAGE_LAYOUT_UNDEFINED = 0;
constexpr u32 VK_QUEUE_FAMILY_IGNORED = ~0u;
struct resource_manager
{
    std::vector<std::unique_ptr<viewable_image>> pending;
    std::function<void()> during_disposal;
    void dispose(std::unique_ptr<viewable_image>& image)
    {
        pending.push_back(std::move(image));
        if (during_disposal)
        {
            auto callback = std::move(during_disposal);
            during_disposal = nullptr;
            callback();
        }
    }
} gc;
resource_manager* get_resource_manager() { return &gc; }
struct basecache
{
    bool handle_memory_pressure(rsx::problem_severity) { return false; }
    void clear() {}
    void on_frame_end() {}
    u32 get_unreleased_textures_count() const { return 0; }
};
struct texture_cache : basecache
{
    using baseclass = basecache;
    struct cached_image_reference_t
    {
        std::unique_ptr<viewable_image> data;
        texture_cache* parent;
        cached_image_reference_t(texture_cache*, std::unique_ptr<viewable_image>&);
        ~cached_image_reference_t();
    };
    struct cached_image_t
    {
        u64 key{};
        std::unique_ptr<viewable_image> data;
        cached_image_t() = default;
        cached_image_t(u64 k, std::unique_ptr<viewable_image>& image) : key(k), data(std::move(image)) {}
    };
    std::deque<cached_image_t> m_cached_images;
    std::atomic<u64> m_cached_memory_size{0};
    mutable shared_mutex m_cached_pool_lock;
    bool m_cache_is_exiting = false;
    std::mutex m_cache_mutex;
    std::unordered_map<u32, std::pair<u32, unsigned*>> m_temporary_subresource_cache;
    struct { u32 m_unreleased_texture_objects = 0; } m_storage;
    u32 m_max_zombie_objects = 10, max_cached_image_pool_size = 256, released_views = 0;
    void release_temporary_subresource(unsigned* view) { delete view; ++released_views; }
    void trim_sections() {}
    void purge_unreleased_sections() {}
    void reset_frame_statistics() {}
    void clear();
    void on_frame_end();
    bool handle_memory_pressure(rsx::problem_severity);
    u32 get_unreleased_textures_count() const;
    std::unique_ptr<viewable_image> find_cached_image(VkFormat, u16, u16, u16, u16, VkImageType, VkImageCreateFlags, VkImageUsageFlags, VkSharingMode);
    void driver_return(u32 id)
    {
        auto image = std::make_unique<viewable_image>();
        image->identity = id;
        cached_image_reference_t completion(this, image);
    }
    std::unique_ptr<viewable_image> lookup(u32 usage = 1)
    {
        return find_cached_image(1, 2, 2, 1, 1, 0, 0, usage, 0);
    }
};
}
#include "VKImagePool.inc"

int main()
{
    using rsx::problem_severity;
    using vk::gc;
    // Pause eviction immediately after its first unique_ptr has been moved.
    // A completed GPU image returns on the driver thread before eviction resumes.
    // That later return must survive, with its bytes accounted for exactly once.
    for (auto severity : {problem_severity::moderate, problem_severity::severe, problem_severity::fatal})
    {
        vk::texture_cache cache;
        cache.driver_return(1);
        if (severity == problem_severity::moderate)
            cache.m_cached_memory_size = 64 * 0x100000;
        gc.during_disposal = [&]
        {
            std::thread driver([&] { cache.driver_return(2); });
            driver.join();
        };
        ensure(cache.handle_memory_pressure(severity));
        ensure(cache.get_unreleased_textures_count() == 1);
        ensure(cache.m_cached_memory_size == 4096);
        auto returned = cache.lookup();
        ensure(returned && returned->identity == 2);
        ensure(!cache.lookup());
        ensure(cache.m_cached_memory_size == 0);
        gc.pending.clear();

        // Returns after the detached batch must survive cleanup with exact bytes.
        cache.driver_return(3);
        gc.during_disposal = [&]
        {
            std::thread driver([&] { cache.driver_return(4); });
            driver.join();
        };
        if (severity == problem_severity::moderate)
            cache.m_cached_memory_size = 64 * 0x100000;
        ensure(cache.handle_memory_pressure(severity));
        ensure(cache.m_cached_memory_size == 4096);
        auto survivor = cache.lookup();
        ensure(survivor && survivor->identity == 4);
        ensure(cache.m_cached_memory_size == 0);
        gc.pending.clear();
    }
    vk::texture_cache cache;
    cache.driver_return(5);
    ensure(!cache.handle_memory_pressure(problem_severity::low));
    ensure(!cache.handle_memory_pressure(problem_severity::moderate));
    ensure(!cache.lookup(4)); // Incompatible usage must leave the resource cached.
    ensure(cache.get_unreleased_textures_count() == 1);
    ensure(cache.lookup()->identity == 5);
    ensure(!cache.lookup());
    for (u32 i = 0; i < 258; ++i) cache.driver_return(i);
    cache.on_frame_end();
    ensure(cache.get_unreleased_textures_count() == 129);
    ensure(cache.m_cached_memory_size == 129 * 4096);
    cache.m_temporary_subresource_cache.emplace(1, std::make_pair(0, new unsigned(1)));
    ensure(cache.handle_memory_pressure(problem_severity::severe));
    ensure(cache.released_views == 1);
    ensure(cache.m_cached_memory_size == 0);
    gc.pending.clear();
    cache.driver_return(6);
    cache.clear();
    std::thread late_driver([&] { cache.driver_return(7); });
    late_driver.join();
    cache.clear();
    ensure(cache.get_unreleased_textures_count() == 0);
    ensure(cache.m_cached_memory_size == 0);
    // The real two-thread schedule: RSX alternates pressure, lookups and frame
    // trimming while the driver returns completed resources to the same deque.
    vk::texture_cache stress;
    std::atomic<bool> begin{false};
    std::thread driver([&]
    {
        while (!begin.load()) std::this_thread::yield();
        for (u32 i = 0; i < 100000; ++i) stress.driver_return(i);
    });
    begin = true;
    while (stress.get_unreleased_textures_count() < 100) std::this_thread::yield();
    for (u32 i = 0; i < 10000; ++i)
    {
        (void)stress.handle_memory_pressure(problem_severity::severe);
        (void)stress.lookup();
        stress.on_frame_end();
        (void)stress.get_unreleased_textures_count();
    }
    driver.join();
    ensure(stress.m_cached_memory_size == u64(stress.get_unreleased_textures_count()) * 4096);
    stress.clear();
    gc.pending.clear();
    std::cout << "Vulkan image pool: " << checks << " ownership/accounting checks passed\n";
}
