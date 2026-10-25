#pragma once

// Independently implemented guest backing pool. The host retains the source
// mappings for the process lifetime. No helper IPC or Objective-C in the core.
#include <algorithm>
#include <array>
#include <new>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>
#include <mach/mach.h>

namespace rpcs3::ios::donation
{
inline constexpr uint64_t granule = 64 * 1024;
inline constexpr uint64_t maximum = 6ull * 1024 * 1024 * 1024;
struct range { uint64_t offset, size; bool dirty; };
struct statistics { uint64_t capacity = 0, live = 0, peak = 0, quarantined = 0, misses = 0; };
struct state
{
    std::mutex mutex;
    unsigned char* base = nullptr;
    std::vector<range> free;
    statistics stats;
};
inline state& pool()
{
    // Static destructors of guest objects may outlive frontend shutdown.
    static auto* value = new state;
    return *value;
}

inline bool offer(void* base, uint64_t size)
{
    auto& p = pool();
    std::lock_guard lock(p.mutex);
    if (p.base || !base || reinterpret_cast<uintptr_t>(base) % granule ||
        !size || size % granule || size > maximum || reinterpret_cast<uintptr_t>(base) > UINTPTR_MAX - size)
        return false;
    // Reject holes, stale pointers and non-writable source ranges at the ABI
    // boundary rather than accepting them and faulting on a later guest boot.
    uintptr_t cursor = reinterpret_cast<uintptr_t>(base);
    const uintptr_t end = cursor + size;
    while (cursor < end)
    {
        vm_address_t address = cursor;
        vm_size_t length = 0;
        vm_region_basic_info_data_64_t info{};
        mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t object = MACH_PORT_NULL;
        const auto kr = ::vm_region_64(mach_task_self(), &address, &length, VM_REGION_BASIC_INFO_64,
            reinterpret_cast<vm_region_info_t>(&info), &count, &object);
        if (object != MACH_PORT_NULL) ::mach_port_deallocate(mach_task_self(), object);
        if (kr != KERN_SUCCESS || address > cursor || !length || address > UINTPTR_MAX - length ||
            address + length <= cursor || (info.protection & (VM_PROT_READ | VM_PROT_WRITE)) != (VM_PROT_READ | VM_PROT_WRITE))
            return false;
        cursor = std::min<uintptr_t>(end, address + length);
    }
    p.free.reserve(size / granule + 1);
    p.free.push_back({0, size, false});
    p.base = static_cast<unsigned char*>(base);
    p.stats.capacity = size;
    return true;
}

inline statistics snapshot()
{
    auto& p = pool();
    std::lock_guard lock(p.mutex);
    return p.stats;
}

class loan
{
    uint64_t m_offset, m_size;
    std::mutex m_mutex;
    std::array<void*, 16> m_views{};
    size_t m_view_count = 0;

public:
    loan(uint64_t offset, uint64_t size) : m_offset(offset), m_size(size) {}
    loan(const loan&) = delete;
    ~loan()
    {
        auto& p = pool();
        std::lock_guard lock(p.mutex);
        p.stats.live -= m_size;
        // Closing a shm handle must not recycle pages still mapped elsewhere.
        // Quarantine surviving aliases rather than corrupt another guest object.
        if (m_view_count) { p.stats.quarantined += m_size; return; }
        p.free.push_back({m_offset, m_size, true});
        std::sort(p.free.begin(), p.free.end(), [](auto a, auto b) { return a.offset < b.offset; });
        for (size_t i = 1; i < p.free.size();)
        {
            auto& a = p.free[i - 1];
            const auto b = p.free[i];
            if (a.offset + a.size == b.offset)
            {
                a.size += b.size;
                a.dirty |= b.dirty;
                p.free.erase(p.free.begin() + i);
            }
            else ++i;
        }
    }

    void* map(void* target, vm_prot_t protection, bool cow, bool overwrite)
    {
        std::lock_guard lock(m_mutex);
        if (m_view_count == m_views.size()) return nullptr;
        vm_address_t address = reinterpret_cast<vm_address_t>(target);
        vm_prot_t current = 0, maximum_protection = 0;
        const auto kr = ::vm_remap(mach_task_self(), &address, m_size, granule - 1,
            target ? (overwrite ? VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE : VM_FLAGS_FIXED) : VM_FLAGS_ANYWHERE,
            mach_task_self(), reinterpret_cast<vm_address_t>(pool().base + m_offset), cow,
            &current, &maximum_protection, VM_INHERIT_NONE);
        if (kr != KERN_SUCCESS) return nullptr;
        if (::vm_protect(mach_task_self(), address, m_size, false, protection) != KERN_SUCCESS)
        {
            ::vm_deallocate(mach_task_self(), address, m_size);
            return nullptr;
        }
        auto* view = reinterpret_cast<void*>(address);
        m_views[m_view_count++] = view;
        return view;
    }

    void unmap(void* view, bool reserve = false)
    {
        std::lock_guard lock(m_mutex);
        auto end = m_views.begin() + m_view_count;
        auto found = std::find(m_views.begin(), end, view);
        if (found == end) return;
        vm_address_t address = reinterpret_cast<vm_address_t>(view);
        const auto kr = reserve
            ? ::vm_map(mach_task_self(), &address, m_size, 0, VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE,
                MACH_PORT_NULL, 0, false, VM_PROT_NONE, VM_PROT_ALL, VM_INHERIT_NONE)
            : ::vm_deallocate(mach_task_self(), address, m_size);
        if (kr == KERN_SUCCESS) *found = m_views[--m_view_count];
    }
};

inline loan* acquire(uint64_t size)
{
    auto& p = pool();
    std::lock_guard lock(p.mutex);
    if (!p.base || !size || size % granule) return nullptr;
    auto best = p.free.end();
    for (auto it = p.free.begin(); it != p.free.end(); ++it)
        if (it->size >= size && (best == p.free.end() || it->size < best->size)) best = it;
    if (best == p.free.end()) { ++p.stats.misses; return nullptr; }
    auto* result = new (std::nothrow) loan(best->offset, size);
    if (!result) { ++p.stats.misses; return nullptr; }
    // Virgin donor memory is zero-filled. Reused data is explicitly scrubbed
    // before publication; never mark live guest backing volatile/purgeable.
    if (best->dirty) std::memset(p.base + best->offset, 0, size);
    best->offset += size;
    best->size -= size;
    if (!best->size) p.free.erase(best);
    p.stats.live += size;
    p.stats.peak = std::max(p.stats.peak, p.stats.live);
    return result;
}
}
