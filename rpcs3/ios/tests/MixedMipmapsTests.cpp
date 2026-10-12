#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <memory>
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using usz = std::size_t;
constexpr u32 CELL_GCM_TEXTURE_A8R8G8B8 = 133;
struct address_range32
{
    u32 start{}, end{};
    static address_range32 start_length(u32 a, u32 n) { assert(n); return {a, a+n-1}; }
    static address_range32 start_end(u32 a, u32 b) { return start_length(a,b-a); }
    address_range32 to_page_range() const { return {start & ~16383u, end | 16383u}; }
};
struct size3f {};
enum class texture_dimension_extended { texture_dimension_2d, other };
enum class texture_upload_context { shader_read, blit_engine_src, framebuffer_storage };
enum class invalidation_cause { read };
struct image_section_attributes_t
{
    u32 address=0xcade0000, gcm_format=133, pitch=1024;
    u16 width=256, height=544, depth=1, mipmaps=5, slice_h=544;
    bool swizzled=false;
};
struct texture_cache_search_options
{
    bool prefer_surface_cache{}, skip_texture_barriers{}, skip_texture_merge{};
};
struct reader_lock { bool available=true; int upgrades=0; bool try_upgrade() { ++upgrades; return available; } };
struct source_image
{
    u16 width{}, height{};
    u64 last_use_tag{};
    bool compatible=true;
    source_image* get_view(auto) { return this; }
    source_image* image() { return this; }
};
struct copy_region_descriptor
{
    source_image* src{};
    u8 level{};
    u16 dst_w{}, dst_h{};
    u32 xform{}, base_addr{};
    u16 src_x{},src_y{},dst_x{},dst_y{},dst_z{},src_w{},src_h{};
};
namespace rsx
{
    template <typename T> using simple_array = std::vector<T>;
    struct renderer { bool tiled=false; bool get_tiled_memory_region(address_range32) const { return tiled; } } current;
    renderer* get_current_renderer() { return &current; }
}
enum class deferred_request_command { nop, copy_image_static, copy_image_dynamic, cubemap_gather, cubemap_unwrap, _3d_gather, _3d_unwrap, atlas_gather, blit_image_static, mipmap_gather };
struct remapping { int encoded=0; };
struct deferred_subresource : image_section_attributes_t
{
    using image_view_type=source_image*;
    std::vector<copy_region_descriptor> sections_to_copy;
    address_range32 cache_range;
    bool do_not_cache=false;
    std::vector<u64> mixed_mipmap_source_tags;
    deferred_request_command op=deferred_request_command::nop;
    remapping remap;
    u64 encoded_properties() const { return width | (u64(height)<<16) | (u64(mipmaps)<<32) | (u64(op)<<40); }
    static deferred_subresource create_mipmap_gather(image_section_attributes_t attr, std::vector<copy_region_descriptor>&& regions, int)
    {
        deferred_subresource d; static_cast<image_section_attributes_t&>(d)=attr;
        d.sections_to_copy=std::move(regions); d.op=deferred_request_command::mipmap_gather; return d;
    }
#include "MixedMipmapSourcesUnderTest.inc"
};
namespace fmt { [[noreturn]] void throw_exception(const char*,u32) { assert(false); __builtin_trap(); } }
struct test_mutex { void lock() {} void unlock() {} };
struct test_atomic { u64 value=1; u64 load() const { return value; } };
struct sampled_image_descriptor
{
    source_image* image_handle=nullptr;
    texture_upload_context upload_context=texture_upload_context::shader_read;
    deferred_subresource desc{};
    bool revalidate_mipmaps=false;
    sampled_image_descriptor() = default;
    sampled_image_descriptor(source_image* p, texture_upload_context c, int, size3f, texture_dimension_extended, u32): image_handle(p),upload_context(c) {}
    sampled_image_descriptor(deferred_subresource d, texture_upload_context c, int, size3f, texture_dimension_extended, u32): upload_context(c),desc(std::move(d)) {}
    bool validate() const { return image_handle || !desc.sections_to_copy.empty(); }
};
namespace helpers
{
    // GPU copy construction is modeled; the lookup/ownership/assembly/order and
    // fallback decision below are extracted from the production cache method.
    template <typename F>
    bool append_mipmap_level(F, std::vector<copy_region_descriptor>& out, const sampled_image_descriptor& image,
        const image_section_attributes_t& attr, u8 level, bool upscale, const image_section_attributes_t&)
    {
        assert(!upscale);
        if (!image.image_handle || !image.image_handle->compatible) return false;
        out.push_back({image.image_handle,level,attr.width,attr.height}); return true;
    }
}
struct guest_texture { int decoded_remap() const { return 0; } };
std::vector<unsigned> get_subresources_layout(guest_texture) { return {0,1,2,3,4}; }
int classify_format(u32) { return 0; }
struct surfaces { using surface_type=source_image*; };
struct cache
{
    using commandbuffer_type=int;
    using image_view_type=source_image*;
    using viewable_image_type=source_image*;
    test_mutex m_cache_mutex;
    test_atomic m_cache_update_tag;
    std::unordered_multimap<u32,std::pair<deferred_subresource,image_view_type>> m_temporary_subresource_cache;
    std::vector<image_view_type> m_uncached_subresources;
    std::vector<std::unique_ptr<source_image>> generated;
    int allocations=0, releases=0;
    image_view_type generate_2d_mipmaps_from_images(int&,deferred_subresource&) {
        ++allocations; generated.emplace_back(new source_image{256,544,0}); return generated.back().get();
    }
    image_view_type generate_cubemap_from_images(int&,deferred_subresource&) { assert(false);return nullptr; }
    image_view_type generate_3d_from_2d_images(int&,deferred_subresource&) { assert(false);return nullptr; }
    image_view_type generate_atlas_from_images(int&,deferred_subresource&) { assert(false);return nullptr; }
    image_view_type create_temporary_subresource_view(int&,deferred_subresource&) { assert(false);return nullptr; }
    void expand_cube_unwrap_sections(std::vector<copy_region_descriptor>&,deferred_subresource&) { assert(false); }
    void expand_3d_unwrap_sections(std::vector<copy_region_descriptor>&,deferred_subresource&,int) { assert(false); }
    void update_image_contents(int&,image_view_type,deferred_subresource&) { assert(false); }
    void release_temporary_subresource(image_view_type) { ++releases; }
#include "MixedMipmapCacheUnderTest.inc"
    bool gpu_base=false, base_cached=false;
    u32 base_version=7;
    source_image base{256,544,7};
    std::map<u32,source_image> gpu;
    std::vector<u32> lookups;
    int uploads=0, invalidations=0;
    struct intersections { bool has_flushables; };
    intersections get_intersecting_set(address_range32 r) { assert((r.start & 16383u)==0 && (r.end & 16383u)==16383); return {gpu_base}; }
    sampled_image_descriptor fast_texture_search(int&, const image_section_attributes_t& attr, size3f scale, int remap,
        texture_cache_search_options options, address_range32 r, texture_dimension_extended dim, surfaces&)
    {
        assert(options.prefer_surface_cache && options.skip_texture_barriers && options.skip_texture_merge);
        assert(attr.mipmaps==1 && r.start==attr.address); assert(remap==0);
        lookups.push_back(attr.address);
        if (auto i=gpu.find(attr.address); i!=gpu.end())
            return {&i->second,texture_upload_context::framebuffer_storage,0,scale,dim,attr.address};
        if (attr.address==0xcade0000 && base_cached && base.last_use_tag==base_version)
            return {&base,texture_upload_context::blit_engine_src,0,scale,dim,attr.address};
        return {};
    }
    void invalidate_range_impl_base(int&, address_range32 r, invalidation_cause)
    {
        assert(!gpu_base); assert(r.start==0xcade0000 && r.end==0xcae67fff); ++invalidations; ++m_cache_update_tag.value;
    }
    source_image* upload_image_from_cpu(int&, address_range32 r, u16 w, u16 h, u16 depth, u16 mips,
        u32 pitch,u32 format,texture_upload_context context,const std::vector<unsigned>& layout,texture_dimension_extended,bool swizzled)
    {
        assert(r.start==0xcade0000 && r.end==0xcae67fff);
        assert(w==256 && h==544 && depth==1 && mips==1 && pitch==1024 && format==133);
        assert(context==texture_upload_context::blit_engine_src && layout==std::vector<unsigned>{0} && !swizzled);
        base_cached=true; base.last_use_tag=base_version; ++uploads; return &base;
    }
#include "MixedMipmapsUnderTest.inc"
};
cache fixture()
{
    cache c;
    // Captured constant-pitch layout, with stale guest-memory copies of levels
    // 1-4 represented by a different version from each current framebuffer.
    c.gpu={{0xcae68000,{256,272,101}}, {0xcaeac000,{256,136,102}},
           {0xcaece000,{256,68,103}}, {0xcaedf000,{256,34,104}}};
    return c;
}
int main()
{
    int cmd=0; guest_texture tex; surfaces ss; reader_lock lock; image_section_attributes_t attr;
    auto invoke=[&](cache& c) { return c.gather_mipmaps_with_cpu_base(cmd,tex,attr,{},texture_dimension_extended::texture_dimension_2d,ss,lock); };
    auto c=fixture(); auto out=invoke(c);
    assert(out.validate() && out.desc.sections_to_copy.size()==5 && out.revalidate_mipmaps && !out.desc.do_not_cache);
    assert(out.desc.cache_range.start==0xcade0000 && out.desc.cache_range.end==0xcaee77ff);
    assert(c.uploads==1 && c.invalidations==1);
    u16 w=256,h=544;
    for (u8 level=0;level<5;++level)
    {
        const auto& region=out.desc.sections_to_copy[level];
        assert(region.level==level && region.dst_w==w && region.dst_h==h);
        assert(region.src->last_use_tag==(level ? 100+level : 7));
        w=std::max<u16>(w/2,1); h=std::max<u16>(h/2,1);
    }
    // Exercise production temporary-cache lookup over thousands of identical
    // binds: the regressing uncached implementation allocates every time.
    auto first=c.create_temporary_subresource(cmd,out.desc);
    for (int bind=0;bind<10000;++bind)
    {
        auto repeated=invoke(c);
        assert(c.create_temporary_subresource(cmd,repeated.desc)==first);
    }
    assert(c.allocations==1 && c.uploads==1 && c.m_uncached_subresources.empty());
    assert(c.m_temporary_subresource_cache.size()==1);
    // Reuse the protected base, but reassemble updated framebuffer levels.
    c.gpu.at(0xcaeac000).last_use_tag=202; out=invoke(c);
    assert(c.uploads==1 && out.desc.sections_to_copy[2].src->last_use_tag==202);
    auto changed=c.create_temporary_subresource(cmd,out.desc);
    assert(changed!=first && c.allocations==2 && c.releases==1);
    assert(c.m_temporary_subresource_cache.size()==1);
    assert(c.create_temporary_subresource(cmd,out.desc)==changed);
    // CPU writes invalidate the base independently of framebuffer contents.
    c.base_version=9; out=invoke(c);
    assert(c.uploads==2 && out.desc.sections_to_copy[0].src->last_use_tag==9);
    auto cpu_changed=c.create_temporary_subresource(cmd,out.desc);
    assert(cpu_changed!=changed && c.allocations==3 && c.releases==2);
    assert(c.m_temporary_subresource_cache.size()==1);
    // Source identity/geometry changes invalidate even with identical timestamps.
    auto altered=out.desc; source_image replacement{128,136,202};
    altered.sections_to_copy[2].src=&replacement;
    assert(!altered.same_mipmap_sources(out.desc));
    altered=out.desc; ++altered.sections_to_copy[2].src_x;
    assert(!altered.same_mipmap_sources(out.desc));
    altered=out.desc; ++altered.mixed_mipmap_source_tags[0];
    assert(!altered.same_mipmap_sources(out.desc));
    deferred_subresource legacy_a,legacy_b;
    assert(legacy_a.same_mipmap_sources(legacy_b));
    assert(!legacy_a.same_mipmap_sources(out.desc));
    for (const u32 missing : {0xcae68000u,0xcaeac000u,0xcaece000u,0xcaedf000u})
    {
        auto miss=fixture(); miss.gpu.erase(missing);
        assert(!invoke(miss).validate() && miss.uploads==0 && miss.invalidations==0);
    }
    auto owned=fixture(); owned.gpu_base=true;
    assert(!invoke(owned).validate() && owned.lookups.empty());
    rsx::current.tiled=true; auto tiled=fixture(); assert(!invoke(tiled).validate()); rsx::current.tiled=false;
    lock.available=false; auto busy=fixture(); assert(!invoke(busy).validate() && busy.lookups.empty()); lock.available=true;
    auto bad=fixture(); bad.gpu.begin()->second.compatible=false; assert(!invoke(bad).validate() && bad.uploads==0);
    for (u16 count : {0,1,14}) { attr.mipmaps=count; auto t=fixture(); assert(!invoke(t).validate() && t.lookups.empty()); } attr.mipmaps=5;
    attr.swizzled=true; assert(!invoke(c).validate()); attr.swizzled=false;
    attr.gcm_format=129; assert(!invoke(c).validate()); attr.gcm_format=133;
    attr.pitch=0; assert(!invoke(c).validate()); attr.pitch=1024;
    assert(!c.gather_mipmaps_with_cpu_base(cmd,tex,attr,{},texture_dimension_extended::other,ss,lock).validate());
    std::puts("Mixed mipmaps: captured five-level layout, source updates, cache reuse and coherent fallbacks passed");
}
