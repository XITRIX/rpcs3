#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>
#include "SPUWideOptimization.h"
using u8=std::uint8_t;using u16=std::uint16_t;using u32=std::uint32_t;using u64=std::uint64_t;
constexpr std::size_t N=1024;
alignas(16) std::array<u8,N*32> a{},b{},c{},expected{},old{},actual{};
u64 seed=0x745ba821841908abull;
u64 random64(){seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;return seed;}
template<class T>T get(const u8* p){T v;std::memcpy(&v,p,sizeof(v));return v;}
template<class T>void put(u8* p,T v){std::memcpy(p,&v,sizeof(v));}
void randomize(){for(auto* x:{&a,&b,&c})for(std::size_t i=0;i<x->size();i+=8)put(x->data()+i,random64());}
void reference(const test& t,const u8* av,const u8* bv,const u8* cv,u8* result){
 std::string_view name=t.name;
 if(name.starts_with("rotate")){unsigned width=name=="rotate16"?16:32;for(unsigned j=0;j<16;j+=width/8){u64 x=width==16?get<u16>(av+j):get<u32>(av+j);unsigned count=(width==16?get<u16>(bv+j):get<u32>(bv+j))&(width-1);u64 v=count?((x<<count)|(x>>(width-count))):x;if(width==16)put(result+j,u16(v));else put(result+j,u32(v));}return;}
 if(name.starts_with("quad")){auto x=get<__uint128_t>(av);unsigned count=get<u32>(bv+12)&7;auto v=name=="quadright"?x>>count:x<<count;if(name=="quadrot"&&count)v|=x>>(128-count);put(result,v);return;}
 if(name.starts_with("byte")){for(int j=0;j<16;++j){int distance=name.starts_with("byteright")?(-t.imm)&31:t.imm&31;int index=name.starts_with("byteright")?j+distance:j-distance;if(name.starts_with("byterot"))index&=15;result[j]=index>=0&&index<16?av[index]:0;}return;}
 if(name.starts_with("compare_")){
  for(int j=0;j<4;++j){u32 x=get<u32>(av+j*4),y=get<u32>(bv+j*4);u32 mx=x&0x7fffffff,my=y&0x7fffffff;
   if(mx<0x800000)x=0;if(my<0x800000)y=0;
   u32 ax=x&0x7fffffff,ay=y&0x7fffffff;bool value=false;
   if(name=="compare_eq")value=x==y;else if(name=="compare_meq")value=ax==ay;else if(name=="compare_mgt")value=ax>ay;
   else if((x>>31)!=(y>>31))value=(x>>31)==0;else value=(x>>31)?ax<ay:ax>ay;
   put(result+j*4,u32(value?~0u:0));
  }return;
 }
 if(name=="cgx"||name=="bgx"){for(int j=0;j<4;++j){u32 x=get<u32>(av+j*4),y=get<u32>(bv+j*4),z=get<u32>(cv+j*4)&1;u32 v=name=="cgx"?u32((u64(x)+y+z)>>32):u32(y>x||(y==x&&z));put(result+j*4,v);}return;}
 std::abort();
}
void check(const test& t){for(std::size_t i=0;i<N*std::size_t(t.bytes);++i)if(old[i]!=expected[i]||actual[i]!=expected[i]){std::fprintf(stderr,"FAIL %s at %zu: old=%02x candidate=%02x expected=%02x input=%016llx\n",t.name,i,old[i],actual[i],expected[i],(unsigned long long)get<u64>(a.data()+(i/8)*8));std::exit(1);}}
int main(int argc,char** argv){
 bool timing=argc<2||std::strcmp(argv[1],"validate");
 const unsigned rounds=argc>2&&std::strcmp(argv[2],"extended")==0?4096:128;
 for(const auto& t:tests){for(unsigned round=0;round<(t.imm<0?rounds:16);++round){randomize();
  if(round<8){for(std::size_t j=0;j<N*t.bytes;j+=4){u32 x=round==0?0:round==1?~0u:round==2?0x80000000u:round==3?1u:u32(1ull<<(j/4%32));put(a.data()+j,x);put(b.data()+j,round==3?~x:x);}}

  if(round==8){constexpr u32 edges[]={0,1,0x7fffff,0x800000,0x800001,0x7fffffff,0x80000000,0x80000001,0x807fffff,0x80800000,0xfffffffe,0xffffffff,15,16,31,32,63,64,127,128,255,256};for(std::size_t j=0;j<N*4;++j){put(a.data()+j*4,edges[j%std::size(edges)]);put(b.data()+j*4,edges[(j/std::size(edges))%std::size(edges)]);}}
  if(round==9){for(std::size_t j=0;j<N*4;++j){u32 x=get<u32>(a.data()+j*4);put(b.data()+j*4,~x);put(c.data()+j*4,u32(j));}}
  if(round==10){for(std::size_t j=0;j<N*4;++j){put(a.data()+j*4,u32(1u<<(j%32)));put(b.data()+j*4,u32(j/32));}}
  for(std::size_t j=0;j<N;++j){std::array<u8,32> av{};std::memcpy(av.data(),a.data()+j*t.bytes,t.bytes);if(t.chain&&j)for(int k=0;k<t.bytes;++k)av[k]^=expected[(j-1)*t.bytes+k];reference(t,av.data(),b.data()+j*t.bytes,c.data()+j*t.bytes,expected.data()+j*t.bytes);}
  t.functions[0](a.data(),b.data(),c.data(),old.data(),N);t.functions[1](a.data(),b.data(),c.data(),actual.data(),N);check(t);
 }if(t.imm<0||t.imm==127)std::printf("PASS %s\n",t.name);std::fflush(stdout);}
 for(const auto& t:tests)if(std::string_view(t.name)=="rotate16"){
  for(unsigned count=0;count<16;++count)for(unsigned start=0;start<65536;start+=N*8){
   for(std::size_t j=0;j<N*8;++j){put(a.data()+j*2,u16(start+j));put(b.data()+j*2,u16(0xff00|count));}
   for(std::size_t j=0;j<N;++j)reference(t,a.data()+j*16,b.data()+j*16,c.data()+j*16,expected.data()+j*16);
   t.functions[0](a.data(),b.data(),c.data(),old.data(),N);t.functions[1](a.data(),b.data(),c.data(),actual.data(),N);check(t);
  }
  std::puts("PASS rotate16: all 65536 values x 16 effective distances");
 }
 if(!timing)return 0;
 randomize();
 constexpr int repeats=8192;
 for(const auto& t:tests){if(t.imm>=0&&t.imm!=0&&t.imm!=5&&t.imm!=16&&t.imm!=31)continue;std::array<std::vector<double>,2> samples;
  for(int round=0;round<12;++round)for(int order=0;order<2;++order){int v=order^(round&1);auto start=std::chrono::steady_clock::now();for(int rep=0;rep<repeats;++rep)t.functions[v](a.data(),b.data(),c.data(),actual.data(),N);double ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/(N*double(repeats));if(round>=2)samples[v].push_back(ns);}
  double med[2];for(int v=0;v<2;++v){auto& s=samples[v];std::sort(s.begin(),s.end());med[v]=(s[4]+s[5])/2;std::printf("%s,%s,%.6f,%.6f,%.6f\n",t.name,v?"candidate":"baseline",med[v],s.front(),s.back());}std::printf("%s improvement %.2f%%\n",t.name,100*(1-med[1]/med[0]));std::fflush(stdout);
 }
}
