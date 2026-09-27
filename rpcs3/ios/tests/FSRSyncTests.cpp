#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>
using u32=uint32_t;
#define ensure(x) assert(x)
struct size2u { u32 width=0,height=0; };
namespace rsx { using flags32_t=u32; }
constexpr u32 UPSCALE_LEFT_VIEW=1,UPSCALE_RIGHT_VIEW=2,UPSCALE_AND_COMMIT=4;
constexpr u32 VK_IMAGE_CREATE_ALLOW_NULL_RPCS3=1,VMM_ALLOCATION_POOL_SWAPCHAIN=0,RSX_FORMAT_CLASS_COLOR=0;
struct logger { template<class... T> void warning(T...) {} template<class... T> void error(T...) {} } rsx_log;
namespace vk {
struct command_buffer { operator VkCommandBuffer() const { return nullptr; } };
struct device {
 bool rgba=true,bgra=true,oom=false;
 struct memory { int device_local=0; };memory get_memory_mapping() { return {}; }
 VkFormatProperties get_format_properties(VkFormat f) {
  VkFormatProperties p{};
  if((f==VK_FORMAT_R8G8B8A8_UNORM&&rgba)||(f==VK_FORMAT_B8G8R8A8_UNORM&&bgra))
   p.optimalTilingFeatures=VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
  return p;
 }
} gpu;
device* get_current_renderer() { return &gpu; }
struct viewable_image {
 VkImage value;VkImageLayout current_layout;VkFormat fmt;u32 w,h;std::vector<VkImageLayout> stack;
 viewable_image(device& d,int,VkMemoryPropertyFlags,VkImageType,VkFormat f,u32 width,u32 height,u32,u32,u32,
 VkSampleCountFlagBits,VkImageLayout layout,VkImageTiling,VkImageUsageFlags,u32,u32,u32):
 value(d.oom?VK_NULL_HANDLE:reinterpret_cast<VkImage>(this)),current_layout(layout),fmt(f),w(width),h(height) {}
 viewable_image():value(reinterpret_cast<VkImage>(this)),current_layout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL),fmt(VK_FORMAT_B8G8R8A8_UNORM),w(320),h(180) {}
 u32 width() const{return w;}u32 height()const{return h;}VkFormat format()const{return fmt;}
 void push_layout(const command_buffer&,VkImageLayout l){stack.push_back(current_layout);current_layout=l;}
 void pop_layout(const command_buffer&){assert(!stack.empty());current_layout=stack.back();stack.pop_back();}
 void change_layout(const command_buffer&,VkImageLayout l){current_layout=l;} // Deliberately provides no compute synchronization.
};
struct manager {template<class T>void dispose(T& p){p.reset();}} resources;
manager* get_resource_manager(){return &resources;}
struct event {VkImage image;VkPipelineStageFlags src,dst;VkAccessFlags sa,da;bool barrier;};
std::vector<event> events;
bool contains(VkPipelineStageFlags value,VkPipelineStageFlags wanted) {return (value&VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)||((value&wanted)==wanted);}
void use(viewable_image* img,VkPipelineStageFlags stage,VkAccessFlags access)
{
 // Find the preceding access and require an intervening barrier for RAW/WAR/WAW.
 auto pos=events.size();
 while(pos && (events[pos-1].barrier || events[pos-1].image!=img->value)) --pos;
 if(pos) {
  auto before=events[pos-1];
  const bool write=access&(VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
  const bool wrote=before.sa&(VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
  if(write||wrote) {
   bool covered=false;
   for(size_t j=pos;j<events.size();++j){auto e=events[j];
    const bool source_access=!wrote||(e.sa&before.sa)||(e.sa&VK_ACCESS_MEMORY_WRITE_BIT);
    const bool dest_access=(e.da&access)||(e.da&(write?VK_ACCESS_MEMORY_WRITE_BIT:VK_ACCESS_MEMORY_READ_BIT));
    if(e.barrier&&e.image==img->value&&contains(e.src,before.src)&&contains(e.dst,stage)&&source_access&&dest_access)covered=true;
   }
   assert(covered);
  }
 }
 events.push_back({img->value,stage,0,access,0,false});
}
void insert_image_memory_barrier(const command_buffer&,VkImage img,VkImageLayout old,VkImageLayout next,
 VkPipelineStageFlags src,VkPipelineStageFlags dst,VkAccessFlags sa,VkAccessFlags da,VkImageSubresourceRange range)
{
 assert(reinterpret_cast<viewable_image*>(img)->current_layout==old);
 assert(next!=VK_IMAGE_LAYOUT_UNDEFINED && range.aspectMask==VK_IMAGE_ASPECT_COLOR_BIT);
 events.push_back({img,src,dst,sa,da,true});
}
namespace FidelityFX {
struct easu_pass {
 void run(const command_buffer&,viewable_image* src,viewable_image* dst,size2u,size2u) {
  assert(src->current_layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL||src->current_layout==VK_IMAGE_LAYOUT_GENERAL);
  assert(dst->current_layout==VK_IMAGE_LAYOUT_GENERAL && dst->format()==VK_FORMAT_R8G8B8A8_UNORM);
  use(src,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
  use(dst,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT);
 }
};struct rcas_pass:easu_pass{};
}
template<class T>T* get_compute_task(){static T t;return &t;}
struct fsr_upscale_pass {
 std::unique_ptr<viewable_image> m_output_left,m_output_right,m_intermediate_data;
 void dispose_images();void initialize_image(u32,u32,rsx::flags32_t);
 viewable_image* scale_output(const command_buffer&,viewable_image*,VkImage,VkImageLayout,const VkImageBlit&,rsx::flags32_t);
};
VkFormat get_renderpass_key(VkFormat format){return format;}
}
VkImageBlit last_blit{};
void vkCmdBlitImage(VkCommandBuffer,VkImage src,VkImageLayout,VkImage,VkImageLayout,u32,const VkImageBlit* b,VkFilter)
{last_blit=*b;vk::use(reinterpret_cast<vk::viewable_image*>(src),VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);}
#include "production.inc"
struct swapchain { VkFormat get_surface_format()const {return VK_FORMAT_R8G8B8A8_UNORM;} };
VkFormat capture([[maybe_unused]] vk::viewable_image* image_to_flip)
{
 [[maybe_unused]]swapchain sc;[[maybe_unused]]auto m_swapchain=&sc;
#include "capture.inc"
}
int main()
{
 vk::command_buffer cmd;unsigned cases=0;
 for(bool commit:{false,true})for(bool right:{false,true})for(bool flip:{false,true})
 {
  vk::events.clear();vk::fsr_upscale_pass fsr;vk::viewable_image input;
  VkImageBlit request{};request.srcOffsets[0]={flip?320:0,0,0};request.srcOffsets[1]={flip?0:320,180,1};request.dstOffsets[1]={640,360,1};
  for(unsigned frame=0;frame<4;++frame)
  {
   if(frame==2){request.dstOffsets[1]={1280,720,1};vk::events.clear();}
   vk::use(&input,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
   auto output=fsr.scale_output(cmd,&input,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,request,
       (right?UPSCALE_RIGHT_VIEW:UPSCALE_LEFT_VIEW)|(commit?UPSCALE_AND_COMMIT:0));
   assert(input.current_layout==VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && input.stack.empty());
   if(commit){assert(!output);assert(last_blit.srcOffsets[0].x==(flip?request.dstOffsets[1].x:0));}
   else {assert(output && output->current_layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);vk::use(output,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);}
   ++cases;
  }
 }
 // Shader-readable input must still make compute reads visible; generic helpers may early-out.
 vk::events.clear();vk::fsr_upscale_pass fsr;vk::viewable_image input;input.current_layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
 VkImageBlit request{};request.srcOffsets[1]={320,180,1};request.dstOffsets[1]={640,360,1};
 vk::use(&input,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
 assert(fsr.scale_output(cmd,&input,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,request,UPSCALE_LEFT_VIEW));
 assert(input.current_layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
 // No rgba8 storage support and allocation failures both retain the bilinear fallback.
 for(bool oom:{false,true}) {
  vk::events.clear();vk::gpu.rgba=oom;vk::gpu.oom=oom;vk::fsr_upscale_pass fail;
  assert(fail.scale_output(cmd,&input,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,request,UPSCALE_LEFT_VIEW)==&input);
  assert(!fail.m_output_left&&!fail.m_intermediate_data);++cases;
 }
 vk::gpu.rgba=true;vk::gpu.oom=false;
 for(VkFormat format:{VK_FORMAT_B8G8R8A8_UNORM,VK_FORMAT_R8G8B8A8_UNORM}){input.fmt=format;assert(capture(&input)==format);++cases;}
 printf("FSR/capture: %u command-stream, reuse, resize, format and fallback cases passed.\n",cases+1);
}
