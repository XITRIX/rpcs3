#include "util/v128.hpp"
#include <arm_neon.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>
#define ARCH_ARM64 1
#define RPCS3_IOS 1
struct RSXFragmentProgram
{
    const void* ptr=nullptr;u32 ucode_length=0,ctrl=0,texture_state=0,texcoord_control_mask=0,mrt_buffers_count=0;
    bool two_sided_lighting=false;
    const void* get_data()const{return ptr;}
};
namespace old
{
struct fragment_program_utils {static bool is_any_src_constant(v128);};
struct fragment_program_compare {static bool compare_properties(const RSXFragmentProgram&,const RSXFragmentProgram&);bool operator()(const RSXFragmentProgram&,const RSXFragmentProgram&)const;};
#include "old-fragment.inc"
}
namespace candidate
{
struct fragment_program_utils {static bool is_any_src_constant(v128);};
struct fragment_program_compare {static bool compare_properties(const RSXFragmentProgram&,const RSXFragmentProgram&);bool operator()(const RSXFragmentProgram&,const RSXFragmentProgram&)const;};
#include "new-fragment.inc"
}
#include "old-fifo.inc"
#include "new-fifo.inc"
volatile u64 sink;
template<typename F>double measure(F f,unsigned count){auto begin=std::chrono::steady_clock::now();u64 sum=0;for(unsigned i=0;i<count;++i){asm volatile("":::"memory");sum+=f(i);}sink=sum;return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-begin).count()/count;}
template<typename A,typename B>void bench(const char* name,A a,B b,unsigned count=500000)
{
    std::vector<double>t[2];
    for(unsigned round=0;round<10;++round)for(unsigned order=0;order<2;++order){unsigned v=(round+order)%2;double ns=v?measure(b,count):measure(a,count);if(round>=2)t[v].push_back(ns);}
    for(auto&v:t)std::sort(v.begin(),v.end());
    double x=(t[0][3]+t[0][4])/2,y=(t[1][3]+t[1][4])/2;
    std::printf("%s baseline=%.3f candidate=%.3f ns/call saving=%.2f%%\n",name,x,y,100*(1-y/x));
}
bool oracle(const RSXFragmentProgram&a,const RSXFragmentProgram&b)
{
    if(a.ucode_length!=b.ucode_length||a.ctrl!=b.ctrl||a.texture_state!=b.texture_state||a.texcoord_control_mask!=b.texcoord_control_mask||a.mrt_buffers_count!=b.mrt_buffers_count||a.two_sided_lighting!=b.two_sided_lighting)return false;
    for(unsigned i=0;i<a.ucode_length/16;){u32 words[4];auto* x=static_cast<const unsigned char*>(a.ptr)+16*i;auto*y=static_cast<const unsigned char*>(b.ptr)+16*i;
        if(std::memcmp(x,y,16))return false;std::memcpy(words,x,16);
        bool constant=false;for(unsigned j=1;j<4;++j)constant|=(words[j]&0x300)==0x200;i+=constant?2:1;
    }return true;
}
void compare(const RSXFragmentProgram&a,const RSXFragmentProgram&b){assert(old::fragment_program_compare{}(a,b)==oracle(a,b));assert(candidate::fragment_program_compare{}(a,b)==oracle(a,b));}
int main(int argc,char**argv)
{
    bool benchmark=argc>1&&std::string(argv[1])=="benchmark";std::mt19937 rng(258);
    std::vector<unsigned char>a(8192+16),b(a.size());
    for(unsigned n=0;n<20000;++n){for(auto&v:a)v=rng();b=a;unsigned size=rng()%513,offset=rng()%16;
        if(n%2&&size)b[offset+rng()%(16*size)]^=1;
        RSXFragmentProgram x{a.data()+offset,size*16},y{b.data()+offset,size*16};compare(x,y);
        for(unsigned bit=0;bit<32;++bit){y=x;y.ctrl^=1u<<bit;compare(x,y);y=x;y.texture_state^=1u<<bit;compare(x,y);}
        y=x;y.mrt_buffers_count^=1;compare(x,y);y=x;y.texcoord_control_mask^=1;compare(x,y);y=x;y.two_sided_lighting=true;compare(x,y);
    }
    RSXFragmentProgram empty;compare(empty,empty);
    auto page=static_cast<usz>(sysconf(_SC_PAGESIZE));auto*mem=static_cast<u8*>(mmap(nullptr,page*3,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0));assert(mem!=MAP_FAILED&&mprotect(mem+page,page,PROT_READ|PROT_WRITE)==0);
    for(unsigned n=16;n<=page;n+=16){RSXFragmentProgram x{mem+page*2-n,n},y{mem+page*2-n,n};compare(x,y);}munmap(mem,page*3);
    for(u32 mask=1;mask<256;++mask)for(int i=-1;i<8;++i)assert(fifo_old(mask,i,8)==fifo_new(mask,i,8));
    for(unsigned n=0;n<200000;++n){u32 mask=rng()|1;for(int i=-1;i<32;++i)assert(fifo_old(mask,i,32)==fifo_new(mask,i,32));}
    if(benchmark)for(unsigned size:{8u,32u}){u32 state=0;auto name="FIFO "+std::to_string(size);bench(name.c_str(),[&](unsigned i){return state=fifo_old(((i*17+1)&(size==8?255u:~0u))|1,state,size);},[&](unsigned i){return state=fifo_new(((i*17+1)&(size==8?255u:~0u))|1,state,size);});}
    std::vector<std::vector<char>>shaders;for(int i=2;i<argc;++i){std::ifstream f(argv[i],std::ios::binary);assert(f);shaders.emplace_back(std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>{});}
    auto copies=shaders;
    for(unsigned mode=0;mode<3&&!shaders.empty();++mode){copies=shaders;if(mode==1)for(auto&s:copies)s[0]^=1;if(mode==2)for(auto&s:copies)for(unsigned i=0;i<s.size()/16;++i){auto v=v128::loadu(s.data(),i);if(old::fragment_program_utils::is_any_src_constant(v)&&++i<s.size()/16)s[i*16]^=1;}
        auto run=[&](unsigned i,bool v){auto j=i%shaders.size();RSXFragmentProgram x{shaders[j].data(),static_cast<u32>(shaders[j].size())},y{copies[j].data(),x.ucode_length};return v?candidate::fragment_program_compare{}(x,y):old::fragment_program_compare{}(x,y);};
        for(unsigned i=0;i<shaders.size();++i)assert(run(i,false)==run(i,true));
        if(benchmark){auto name="fragment corpus mode "+std::to_string(mode);bench(name.c_str(),[&](unsigned i){return run(i,false);},[&](unsigned i){return run(i,true);},100000);}
    }
    std::puts("PASS: fragment scalar oracle/metadata/guard pages; FIFO exhaustive 8-line and 6.6M 32-line transitions");
}
