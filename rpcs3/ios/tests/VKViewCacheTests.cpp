#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
using u32=std::uint32_t;using u64=std::uint64_t;using VkImageAspectFlags=u32;
constexpr u32 VK_COMPONENT_SWIZZLE_A=3,VK_COMPONENT_SWIZZLE_R=0,VK_COMPONENT_SWIZZLE_G=1,VK_COMPONENT_SWIZZLE_B=2,VK_COMPONENT_SWIZZLE_IDENTITY=4;
constexpr u32 VK_REMAP_IDENTITY=0xff,RSX_TEXTURE_REMAP_IDENTITY=0xfe,VK_IMAGE_VIEW_TYPE_MAX_ENUM=99,VK_NULL_HANDLE=0;
struct VkComponentMapping {u32 r,g,b,a;bool operator==(const VkComponentMapping&)const=default;};
struct VkImageSubresourceRange {u32 aspectMask,baseMipLevel,levelCount,baseArrayLayer,layerCount;};
namespace rsx {struct texture_channel_remap_t{u32 encoded;};}
#define ensure(x) assert(x)
namespace vk
{
int device=1;int* g_render_device=&device;unsigned creations=0,lookups=0;
VkComponentMapping apply_swizzle_remap(std::array<u32,4> native,rsx::texture_channel_remap_t remap){return {native[(remap.encoded>>0)&3],native[(remap.encoded>>2)&3],native[(remap.encoded>>4)&3],native[(remap.encoded>>6)&3]};}
struct viewable_image;
struct image_view
{
    struct {VkComponentMapping components;VkImageSubresourceRange subresourceRange;} info;
    unsigned id;
    image_view(int,viewable_image*,int,u32,VkComponentMapping map,VkImageSubresourceRange range):info{map,range},id(++creations){}
};
struct gc {void dispose(std::unique_ptr<image_view>& p){p.reset();}} collector;
gc* get_resource_manager(){return &collector;}
struct view_map:std::unordered_map<u64,std::unique_ptr<image_view>>
{
    auto find(u64 k){++lookups;return std::unordered_map<u64,std::unique_ptr<image_view>>::find(k);}
};
struct viewable_image
{
    VkComponentMapping native_component_map{0,1,2,3};
    struct {u32 mipLevels=5,arrayLayers=2;} info;
    view_map views;u64 m_last_view_key=0;image_view* m_last_view=nullptr;
    int m_device=1,value=1;std::unique_ptr<int> memory;
    int format()const{return 1;}u32 aspect()const{return 7;}
    image_view* get_view(const rsx::texture_channel_remap_t&,VkImageAspectFlags);
    void set_native_component_layout(VkComponentMapping);
    viewable_image* clone();
};
#include "ViewMethods.inc"
}
int main(int argc,char**argv)
{
    assert(argc==2);bool candidate=std::string(argv[1])=="candidate";using namespace vk;
    viewable_image image;auto* first=image.get_view({RSX_TEXTURE_REMAP_IDENTITY},1);
    unsigned first_id=first->id;lookups=0;
    for(int i=0;i<10000;++i)assert(image.get_view({VK_REMAP_IDENTITY},1)==first);
    assert(lookups==(candidate?0u:10000u));printf("%s: repeated image-view lookup probes %u / 10000\n",argv[1],lookups);
    // All remaps and aspect keys coexist; changing the last key cannot alias.
    for(u32 remap=0;remap<256;++remap)for(u32 mask=1;mask<8;++mask)
    {
        auto* a=image.get_view({remap},mask);auto* b=image.get_view({remap},mask);assert(a==b);
        assert(a->info.subresourceRange.aspectMask==mask&&a->info.subresourceRange.levelCount==5&&a->info.subresourceRange.layerCount==2);
        auto expected=remap>=RSX_TEXTURE_REMAP_IDENTITY?VkComponentMapping{0,1,2,3}:apply_swizzle_remap({3,0,1,2},{remap});
        assert(a->info.components==expected);
    }
    // Same layout preserves views. A different layout invalidates the last hit.
    assert(image.get_view({VK_REMAP_IDENTITY},1)->id==first_id);
    image.set_native_component_layout({0,1,2,3});assert(image.get_view({VK_REMAP_IDENTITY},1)->id==first_id);
    image.set_native_component_layout({2,1,0,3});auto* changed=image.get_view({RSX_TEXTURE_REMAP_IDENTITY},1);
    assert(changed->id!=first_id&&(changed->info.components==VkComponentMapping{2,1,0,3}));
    // Destructive clone transfers ownership; rebuilding the source must not
    // return a cached pointer owned (and possibly destroyed) by the clone.
    auto old_id=changed->id;std::unique_ptr<viewable_image> copy(image.clone());
    assert(image.value==0&&copy->value==1);copy.reset();image.value=2;
    assert(image.get_view({RSX_TEXTURE_REMAP_IDENTITY},1)->id!=old_id);
    printf("%s: 1792 remap/aspect pairs, identity canonicalization, layout invalidation and destructive clone passed\n",argv[1]);
}
