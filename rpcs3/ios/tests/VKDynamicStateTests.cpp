#include "Emu/RSX/VK/vkutils/dynamic_state_cache.hpp"
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <random>
using u32=std::uint32_t;using VkStencilFaceFlags=u32;using VkPipeline=u32;using VkPipelineBindPoint=u32;using VkDescriptorSet=u32;
constexpr u32 VK_PIPELINE_BIND_POINT_COMPUTE=1,VK_PIPELINE_BIND_POINT_GRAPHICS=0,VK_NULL_HANDLE=0;
constexpr u32 VK_STENCIL_FACE_FRONT_BIT=1,VK_STENCIL_FACE_BACK_BIT=2;
#define ensure(x) assert(x)
struct driver {std::array<std::array<u32,4>,10> values{};u32 valid=0;unsigned calls=0;} states[2];
void write(unsigned cmd,unsigned index,std::array<u32,4> value){auto& d=states[cmd];d.values[index]=value;d.valid|=1u<<index;}
void vkCmdBindPipeline(unsigned cmd,VkPipelineBindPoint point,VkPipeline){if(point==0)states[cmd].valid=0;}
void vkCmdSetLineWidth(unsigned cmd,float value){++states[cmd].calls;write(cmd,0,{std::bit_cast<u32>(value)});}
void vkCmdSetBlendConstants(unsigned cmd,const float* value){++states[cmd].calls;write(cmd,1,{std::bit_cast<u32>(value[0]),std::bit_cast<u32>(value[1]),std::bit_cast<u32>(value[2]),std::bit_cast<u32>(value[3])});}
void vkCmdSetDepthBias(unsigned cmd,float a,float b,float c){++states[cmd].calls;write(cmd,2,{std::bit_cast<u32>(a),std::bit_cast<u32>(b),std::bit_cast<u32>(c)});}
void vkCmdSetDepthBounds(unsigned cmd,float a,float b){++states[cmd].calls;write(cmd,3,{std::bit_cast<u32>(a),std::bit_cast<u32>(b)});}
void stencil(unsigned cmd,unsigned slot,u32 faces,u32 value){++states[cmd].calls;if(faces&1)write(cmd,slot,{value});if(faces&2)write(cmd,slot+1,{value});}
void vkCmdSetStencilWriteMask(unsigned cmd,u32 faces,u32 value){stencil(cmd,4,faces,value);}
void vkCmdSetStencilCompareMask(unsigned cmd,u32 faces,u32 value){stencil(cmd,6,faces,value);}
void vkCmdSetStencilReference(unsigned cmd,u32 faces,u32 value){stencil(cmd,8,faces,value);}
namespace vk
{
struct command_buffer
{
    bool is_open=true;unsigned commands=1;
    mutable std::array<VkPipeline,2> m_bound_pipelines{};
    mutable std::array<VkDescriptorSet,2> m_bound_descriptor_sets{};
    mutable dynamic_state_cache m_dynamic_state;
#include "DynamicMethods.inc"
};
}
void verify(){assert(states[0].valid==states[1].valid);for(unsigned i=0;i<10;++i)if(states[0].valid&(1u<<i))assert(states[0].values[i]==states[1].values[i]);}
int main()
{
    vk::command_buffer cmd;std::mt19937 rng(258);unsigned cases=0;
    auto set=[&](unsigned kind,u32 faces,std::array<u32,4> values){std::array<float,4> f;for(unsigned i=0;i<4;++i)f[i]=std::bit_cast<float>(values[i]);
        switch(kind){
        case 0:vkCmdSetLineWidth(0,f[0]);cmd.set_line_width(f[0]);break;
        case 1:vkCmdSetBlendConstants(0,f.data());cmd.set_blend_constants(f.data());break;
        case 2:vkCmdSetDepthBias(0,f[0],f[1],f[2]);cmd.set_depth_bias(f[0],f[1],f[2]);break;
        case 3:vkCmdSetDepthBounds(0,f[0],f[1]);cmd.set_depth_bounds(f[0],f[1]);break;
        case 4:vkCmdSetStencilWriteMask(0,faces,values[0]);cmd.set_stencil_write_mask(faces,values[0]);break;
        case 5:vkCmdSetStencilCompareMask(0,faces,values[0]);cmd.set_stencil_compare_mask(faces,values[0]);break;
        case 6:vkCmdSetStencilReference(0,faces,values[0]);cmd.set_stencil_reference(faces,values[0]);break;
        }verify();++cases;
    };
    for(unsigned i=0;i<10000;++i)for(unsigned kind=0;kind<7;++kind)set(kind,3,{0,1,2,3});
    assert(states[0].calls==70000&&states[1].calls==7);
    std::printf("repeated state: driver calls %u -> %u\n",states[0].calls,states[1].calls);
    // The first value after invalidation may equal the old one. Random inputs
    // alone almost never exercise that essential re-emission requirement.
    for(unsigned reset=0;reset<3;++reset){
        for(unsigned kind=0;kind<7;++kind)set(kind,3,{0,1,2,3});
        const auto calls=states[1].calls;
        if(reset==0){cmd.clear_state_cache();states[0].valid=states[1].valid=0;}
        if(reset==1){vkCmdBindPipeline(0,0,42);cmd.bind_pipeline(42,0);}
        if(reset==2){cmd.bind_pipeline(43,1);}
        for(unsigned kind=0;kind<7;++kind)set(kind,3,{0,1,2,3});
        assert(states[1].calls-calls==(reset==2?0u:7u));
    }
    for(u32 bits:{0u,0x80000000u,0x7f800000u,0xff800000u,0x7fc00001u,0x7fc00002u,0xffffffffu})for(unsigned kind=0;kind<7;++kind)for(unsigned faces=1;faces<4;++faces)set(kind,faces,{bits,bits,bits,bits});
    for(unsigned i=0;i<100000;++i){
        if(i%257==0){cmd.clear_state_cache();states[0].valid=states[1].valid=0;}
        if(i%113==0){auto pipeline=static_cast<u32>(i+1);vkCmdBindPipeline(0,0,pipeline);cmd.bind_pipeline(pipeline,0);}
        if(i%37==0){cmd.bind_pipeline(static_cast<u32>(i+1),1);}
        set(rng()%7,1+rng()%3,{rng(),rng(),rng(),rng()});
    }
    std::printf("PASS dynamic state: %u differential commands; recording/pipeline reset, compute independence, per-face stencil, signed zero and NaN payloads\n",cases);
}
