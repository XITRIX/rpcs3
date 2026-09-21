#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>
using u32 = std::uint32_t;
using u16 = std::uint16_t;
struct address_range32 {};
constexpr int full_range = 0;
struct logger { template <typename... T> void warning(const char*, T...) {} } rsx_log;

namespace rsx
{
namespace texture_upload_context
{
constexpr u32 shader_read = 1, blit_engine_dst = 2, blit_engine_src = 4;
}
template <typename T> struct simple_array : std::vector<T>
{
    template <typename F> void erase_if(F predicate) { std::erase_if(*this, predicate); }
};
template <bool, typename T> bool pitch_compatible(T* texture, u32 pitch, int)
{
    return texture->pitch == pitch;
}
}

struct texture
{
    int id = 0;
    u32 context = rsx::texture_upload_context::shader_read;
    u32 address = 1, format = 1, pitch = 16;
    u16 width = 4, height = 4, depth = 1;
    bool dirty = false, swizzled = false;
    int validations = 0;
    std::array<unsigned char, 512> source{}, uploaded{};
    bool is_dirty() const { return dirty; }
    u32 get_context() const { return context; }
    bool matches(u32 a, u32 f, u16 w, u16 h, u16 d, u16) const
    {
        return address == a && format == f && width == w && height == h && depth == d;
    }
    bool sync_protection()
    {
        ++validations;
        if (source != uploaded) dirty = true;
        return !dirty;
    }
    bool is_swizzled() const { return swizzled; }
    void unprotect() {}
    void set_dirty(bool value) { dirty = value; }
    int get_view(int) const { return id; }
    int get_format_class() const { return 0; }
    int get_image_type() const { return 0; }
    u32 get_section_base() const { return address; }
};
struct attributes
{
    u32 address = 1, gcm_format = 1, pitch = 16;
    u16 width = 4, height = 4, depth = 1;
    bool swizzled = false;
};
struct search_options { u32 lookup_mask = 7; };
struct lookup_result
{
    int view = 0;
    u32 context = 0;
    int format_class = 0, scale = 0, image_type = 0;
    u32 base = 0;
    rsx::simple_array<texture*> merge{};
};
struct storage
{
    std::vector<texture> textures;
    auto range_begin(const address_range32&, int, bool) { return textures.begin(); }
    auto range_end() { return textures.end(); }
};
struct cache
{
    using section_storage_type = texture;
    storage m_storage;
#include "TextureCollectionUnderTest.inc"
    lookup_result lookup(const attributes& attr = {}, const search_options& options = {})
    {
        address_range32 memory_range;
        rsx::simple_array<texture*> overlapping_locals;
        const int remap = 0, scale = 1;
#include "TextureSelectionUnderTest.inc"
        lookup_result result;
        result.merge = overlapping_locals;
        return result;
    }
};

texture make(int id, u32 context = rsx::texture_upload_context::shader_read, bool exact = true)
{
    texture result;
    result.id = id; result.context = context;
    if (!exact) result.width = 8;
    return result;
}
std::vector<int> ids(const lookup_result& r)
{
    std::vector<int> result;
    for (auto* t : r.merge) result.push_back(t->id);
    return result;
}
// Independent eager-validation oracle for observable selection, including the
// original first matching swizzle-mismatch rule. Unused dirty flags may differ.
std::pair<int, std::vector<int>> oracle(std::vector<texture> ts, const attributes& attr, const search_options& options)
{
    std::vector<texture*> valid;
    for (auto& t : ts)
        if (!t.dirty && (t.context & options.lookup_mask) && (attr.height <= 1 || t.pitch == attr.pitch) && t.source == t.uploaded)
            valid.push_back(&t);
    for (auto* t : valid)
        if (t->matches(attr.address, attr.gcm_format, attr.width, attr.height, attr.depth, 0))
        {
            if (t->swizzled != attr.swizzled) { t->dirty = true; break; }
            return {t->id, {}};
        }
    std::vector<int> merge;
    for (auto* t : valid) if (!t->dirty && t->context == rsx::texture_upload_context::blit_engine_dst) merge.push_back(t->id);
    return {0, merge};
}
int main()
{
    using namespace rsx::texture_upload_context;
    cache c;
    c.m_storage.textures = {make(1, shader_read, false), make(2), make(3, blit_engine_dst, false)};
    assert(c.lookup().view == 2);
    assert(c.m_storage.textures[0].validations == 0);
    assert(c.m_storage.textures[1].validations == 1);
    assert(c.m_storage.textures[2].validations == 0);
    // Even two adjacent lookups must detect a write at the end of the range.
    c.m_storage.textures[1].source.back() = 1;
    assert(ids(c.lookup()) == std::vector<int>{3});
    assert(c.m_storage.textures[1].dirty);
    assert(c.m_storage.textures[2].validations == 1);
    c.m_storage.textures[2].source.front() = 1;
    assert(c.lookup().merge.empty());
    // Invalid first exact hit must not hide the next valid exact candidate.
    c.m_storage.textures = {make(1), make(2)};
    c.m_storage.textures[0].source[256] = 1;
    assert(c.lookup().view == 2);
    // Existing public range lookups must still eagerly validate all entries.
    c.m_storage.textures = {make(1), make(2, blit_engine_dst)};
    c.m_storage.textures[0].source[300] = 1;
    auto range = c.find_texture_from_range(address_range32{});
    assert(range.size() == 1 && range[0]->id == 2);
    assert(c.m_storage.textures[0].validations == 1 && c.m_storage.textures[1].validations == 1);

    std::mt19937 rng(225);
    for (int trial = 0; trial < 20000; ++trial)
    {
        c.m_storage.textures.clear();
        for (int i = 1; i <= 12; ++i)
        {
            auto t = make(i, 1u << (rng() % 4), rng() % 2);
            t.pitch = rng() % 3 ? 16 : 32;
            t.dirty = rng() % 8 == 0;
            t.swizzled = rng() % 5 == 0;
            if (rng() % 4 == 0) t.source[rng() % t.source.size()] = 1;
            c.m_storage.textures.push_back(t);
        }
        attributes attr;
        if (rng() % 4 == 0) attr.height = 1;
        search_options options{rng() & 7};
        auto expected = oracle(c.m_storage.textures, attr, options);
        auto result = c.lookup(attr, options);
        assert(result.view == expected.first && ids(result) == expected.second);
        if (result.view)
            for (const auto& t : c.m_storage.textures)
                if (t.id == result.view) assert(t.validations > 0 && t.source == t.uploaded);
        for (auto* t : result.merge) assert(t->validations > 0 && t->source == t->uploaded);
    }
    std::puts("Texture lookup: unused candidates skipped, content changes rejected, eager API preserved, 20000 selection comparisons passed");
}
