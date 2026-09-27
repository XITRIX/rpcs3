#include "util/v128.hpp"
#include "Emu/CPU/sse2neon.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <span>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>
#define RPCS3_IOS 1
namespace utils {template<typename T,typename U>T* bless(U* p){return reinterpret_cast<T*>(p);}}
struct RSXFragmentProgram {const void* ptr;const void* get_data()const{return ptr;}};
#include "old-constants.inc"
#include "new-constants.inc"
volatile u64 sink;
int main(int argc,char**)
{
 std::mt19937 rng(258);alignas(16) u32 source[4096],a[4096],b[4096];
 for(auto&v:source)v=rng();
 std::vector<u32> offsets;
 for(unsigned count=0;count<=256;++count)for(bool sanitize:{false,true}){
  offsets.clear();for(unsigned i=0;i<count;++i)offsets.push_back(rng()%16000);
  std::memset(a,0xcc,sizeof(a));std::memset(b,0xcc,sizeof(b));
  RSXFragmentProgram prog{source};std::span<float>x{reinterpret_cast<float*>(a),count*4},y{reinterpret_cast<float*>(b),count*4};
  upload_old(x,prog,offsets,sanitize);upload_new(y,prog,offsets,sanitize);assert(std::memcmp(a,b,sizeof(a))==0);
  for(unsigned i=0;i<count;++i)for(unsigned j=0;j<4;++j){u32 v;std::memcpy(&v,reinterpret_cast<u8*>(source)+offsets[i]+j*4,4);v=((v>>8)&0x00ff00ff)|((v<<8)&0xff00ff00);if(sanitize&&(v&0x7fffffff)>=0x7f800000)v=0;assert(b[i*4+j]==v);}
 }
 // All exponent/sign combinations and diverse NaN payloads, including signed zero.
 offsets={0};for(u32 exponent=0;exponent<512;++exponent)for(u32 payload:{0u,1u,0x12345u,0x7fffffu}){u32 v=(exponent<<23)|payload;source[0]=((v>>8)&0x00ff00ff)|((v<<8)&0xff00ff00);upload_new({reinterpret_cast<float*>(b),4},{source},offsets,true);assert(b[0]==((v&0x7fffffff)>=0x7f800000?0:v));}
 auto page=static_cast<usz>(sysconf(_SC_PAGESIZE));auto*mem=static_cast<u8*>(mmap(nullptr,page*3,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0));assert(mem!=MAP_FAILED&&mprotect(mem+page,page,PROT_READ|PROT_WRITE)==0);upload_new({reinterpret_cast<float*>(b),4},{mem+page*2-16},offsets,true);munmap(mem,page*3);
 puts("PASS: constant upload scalar bits, randomized offsets, finite/NaN/Inf, signed zero, sentinels and guard pages");if(argc==1)return 0;
 for(auto&v:source)v=rng();for(unsigned count:{1u,4u,16u,64u,256u})for(bool sanitize:{false,true}){offsets.clear();for(unsigned i=0;i<count;++i)offsets.push_back(i*32);std::vector<double>times[2];
  for(unsigned round=0;round<10;++round)for(unsigned order=0;order<2;++order){unsigned v=(round+order)%2;auto start=std::chrono::steady_clock::now();for(unsigned i=0;i<500000;++i){asm volatile("":::"memory");if(v)upload_new({reinterpret_cast<float*>(b),count*4},{source},offsets,sanitize);else upload_old({reinterpret_cast<float*>(b),count*4},{source},offsets,sanitize);}sink=b[0];double ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/500000;if(round>=2)times[v].push_back(ns);}
  for(auto&t:times)std::sort(t.begin(),t.end());double x=(times[0][3]+times[0][4])/2,y=(times[1][3]+times[1][4])/2;printf("constants %u sanitize %d %.3f -> %.3f ns saving %.2f%%\n",count,sanitize,x,y,100*(1-y/x));
 }
}
