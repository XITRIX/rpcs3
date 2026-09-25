#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

using u8=std::uint8_t; using u16=std::uint16_t; using u32=std::uint32_t; using u64=std::uint64_t;
using unary=void(*)(const void*,void*,u64);
using binary=void(*)(const void*,const void*,void*,u64);
using ternary=void(*)(const void*,const void*,const void*,void*,u64);
#define PAIR1(n) extern "C" void n##_baseline(const void*,void*,u64); extern "C" void n##_candidate(const void*,void*,u64)
#define PAIR2(n) extern "C" void n##_baseline(const void*,const void*,void*,u64); extern "C" void n##_candidate(const void*,const void*,void*,u64)
#define PAIR3(n) extern "C" void n##_baseline(const void*,const void*,const void*,void*,u64); extern "C" void n##_candidate(const void*,const void*,const void*,void*,u64)
PAIR3(insert16);PAIR3(insert32);PAIR3(insert64);PAIR3(mask8);PAIR3(mask16);PAIR3(mask32);PAIR3(mask64);PAIR3(shuffle);
PAIR1(extend8);PAIR1(extend16);PAIR1(extend32);
PAIR2(ashr16);PAIR2(ashr32);
PAIR2(extend_chain8);PAIR2(extend_chain16);PAIR2(extend_chain32);
constexpr std::size_t N=1024;
using bytes=std::array<u8,N*16>;
alignas(16) bytes a{},b{},c{},old{},actual{},expected{};
std::array<u32,N> indices{};
std::array<u64,N> elements{};
u64 rng=0xb782a490c85147e7ull;
u64 random64(){rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return rng;}
void fill_random(bytes& buffer){for(std::size_t i=0;i<buffer.size();i+=8){u64 v=random64();std::memcpy(buffer.data()+i,&v,8);}}
u64 get(const bytes& buffer,std::size_t offset,unsigned width){u64 v=0;std::memcpy(&v,buffer.data()+offset,width/8);return v;}
void put(bytes& buffer,std::size_t offset,unsigned width,u64 value){std::memcpy(buffer.data()+offset,&value,width/8);}
void check(const char* name,std::size_t size){for(std::size_t i=0;i<size;++i)if(old[i]!=expected[i]||actual[i]!=expected[i]){std::fprintf(stderr,"FAIL %s byte %zu: old=%02x new=%02x expected=%02x\n",name,i,old[i],actual[i],expected[i]);std::exit(1);}}
struct insertion{const char* name;unsigned width;bool mask;ternary fn[2];};
const insertion insertions[]={
 {"insert16",16,false,{insert16_baseline,insert16_candidate}}, {"insert32",32,false,{insert32_baseline,insert32_candidate}}, {"insert64",64,false,{insert64_baseline,insert64_candidate}},
 {"mask8",8,true,{mask8_baseline,mask8_candidate}}, {"mask16",16,true,{mask16_baseline,mask16_candidate}}, {"mask32",32,true,{mask32_baseline,mask32_candidate}}, {"mask64",64,true,{mask64_baseline,mask64_candidate}}};
struct extension{const char* name;unsigned width;unary fn[2];};
const extension extensions[]={{"extend8",8,{extend8_baseline,extend8_candidate}},{"extend16",16,{extend16_baseline,extend16_candidate}},{"extend32",32,{extend32_baseline,extend32_candidate}}};
struct shift{const char* name;unsigned width;binary fn[2];};
const shift shifts[]={{"ashr16",16,{ashr16_baseline,ashr16_candidate}},{"ashr32",32,{ashr32_baseline,ashr32_candidate}}};
struct extension_chain { const char* name; unsigned width; binary fn[2]; };
const extension_chain extension_chains[] = {{"extend_chain8",8,{extend_chain8_baseline,extend_chain8_candidate}}, {"extend_chain16",16,{extend_chain16_baseline,extend_chain16_candidate}}, {"extend_chain32",32,{extend_chain32_baseline,extend_chain32_candidate}}};

template<class Work>void measure(const char* name,Work work,double operations){
 std::array<std::vector<double>,2> samples;
 for(int round=0;round<12;++round)for(int order=0;order<2;++order){int which=order^(round&1);auto start=std::chrono::steady_clock::now();work(which);double ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/operations;if(round>=2)samples[which].push_back(ns);}
 double median[2];for(int j=0;j<2;++j){auto& v=samples[j];std::sort(v.begin(),v.end());median[j]=(v[4]+v[5])/2;std::printf("%s,%s,%.6f,%.6f,%.6f\n",name,j?"candidate":"baseline",median[j],v.front(),v.back());}
 std::printf("%s cost reduction %.2f%%\n",name,100*(1-median[1]/median[0]));std::fflush(stdout);
}

int main(int argc,char** argv){
 bool exhaustive=argc>1&&!std::strcmp(argv[1],"exhaustive");
 for(const auto& test:insertions){
  const unsigned bytes_per=test.width/8,lanes=16/bytes_per;u64 cases=0;
  for(u32 seed=0;seed<4;++seed)for(u32 value=0;value<65536;++value)for(u32 lane=0;lane<lanes;++lane){
   for(unsigned i=0;i<16;++i)a[i]=u8(seed*73+i*17);
   if(test.mask){for(unsigned i=0;i<16;++i)expected[i]=u8(31-i);}else std::memcpy(expected.data(),a.data(),16);
   u64 element=test.width==16?value:(u64(value)*0x9e3779b97f4a7c15ull)^(u64(seed)<<48);
   elements[0]=element;indices[0]=lane;
   if(test.mask)element=test.width==64?0x0001020304050607ull:0x10203ull;
   put(expected,lane*bytes_per,test.width,element);
   for(int j=0;j<2;++j)test.fn[j](a.data(),elements.data(),indices.data(),j?actual.data():old.data(),1);
   check(test.name,16);++cases;
  }
  // A dependency chain catches corruption of untouched lanes across updates.
  fill_random(a);fill_random(b);for(std::size_t i=0;i<N;++i)indices[i]=u32(random64())&(lanes-1);
  std::memcpy(expected.data(),a.data(),16);
  for(std::size_t i=0;i<N;++i){
   if(test.mask){for(unsigned j=0;j<16;++j)expected[i*16+j]=u8(31-j);put(expected,i*16+indices[i]*bytes_per,test.width,test.width==64?0x1020304050607ull:0x10203ull);}
   else put(expected,indices[i]*bytes_per,test.width,get(b,i*bytes_per,test.width));
  }
  test.fn[0](a.data(),b.data(),indices.data(),old.data(),N);test.fn[1](a.data(),b.data(),indices.data(),actual.data(),N);check(test.name,test.mask?N*16:16);
  std::printf("PASS %s: %llu element/lane/base cases and %zu updates\n",test.name,(unsigned long long)cases,N);std::fflush(stdout);
 }
 for(const auto& test:extensions){
  unsigned lanes=128/test.width;u64 cases=1ull<<lanes;
  for(u64 base=0;base<cases;base+=N){u64 count=std::min<u64>(N,cases-base);
   for(u64 i=0;i<count;++i)for(unsigned lane=0;lane<lanes;++lane)put(a,i*16+lane*test.width/8,test.width,((base+i)>>lane)&1?~0ull:0);
   for(u64 i=0;i<count;++i)for(unsigned lane=0;lane<lanes/2;++lane)put(expected,i*16+lane*test.width/4,test.width*2,((base+i)>>(2*lane))&1?~0ull:0);
   test.fn[0](a.data(),old.data(),count);test.fn[1](a.data(),actual.data(),count);check(test.name,count*16);
  }
  std::printf("PASS %s: all %llu Boolean-mask combinations\n",test.name,(unsigned long long)cases);
 }
 for(const auto& test:shifts){
  unsigned width=test.width,lanes=128/width;u64 total=test.width==16?(1ull<<21):(exhaustive?(1ull<<32):(1ull<<22));
  for(u64 base=0;base<total;base+=N*lanes){
   for(std::size_t i=0;i<N*lanes;++i){u64 ordinal=base+i;u32 raw=width==16?u32(ordinal>>5):u32(ordinal)*0x9e3779b1u;u32 amount=width==16?u32(ordinal&31):u32((ordinal>>7)^ordinal)&63;
    u32 count=u32(0-amount)|((u32(random64())&0xffffu)<<(width==16?5:6)); // Higher bits are ignored by SPU.
    put(a,i*width/8,width,raw);put(b,i*width/8,width,count);
    u64 bits=get(a,i*width/8,width),sign=bits>>(width-1);u32 distance=u32(0-count)&(2*width-1);
    u64 result=distance>=width?(sign?~0ull:0):(bits>>distance)|((sign&&distance)?(~0ull<<(width-distance)):0);
    put(expected,i*width/8,width,result);
   }
   test.fn[0](a.data(),b.data(),old.data(),N);test.fn[1](a.data(),b.data(),actual.data(),N);check(test.name,N*16);
  }
  // Integer extremes with every effective shift distance.
  for(unsigned count=0;count<2*width;++count)for(unsigned bit=0;bit<width;++bit)for(int delta=-1;delta<=1;++delta){u64 v=(1ull<<bit)+delta;
   for(std::size_t i=0;i<N*lanes;++i){put(a,i*width/8,width,v);put(b,i*width/8,width,u64(0-count));u64 bits=get(a,i*width/8,width),sign=bits>>(width-1);u64 result=count>=width?(sign?~0ull:0):(bits>>count)|((sign&&count)?(~0ull<<(width-count)):0);put(expected,i*width/8,width,result);}
   test.fn[0](a.data(),b.data(),old.data(),N);test.fn[1](a.data(),b.data(),actual.data(),N);check(test.name,N*16);
  }
  std::printf("PASS %s: %llu pairs plus every bit-boundary/count combination\n",test.name,(unsigned long long)total);std::fflush(stdout);
 }
 for(unsigned selector=0;selector<256;++selector){fill_random(b);fill_random(c);for(std::size_t i=0;i<a.size();++i){a[i]=u8(selector+i);u8 s=a[i]|128;expected[i]=s<192?0:s<224?255:128;}shuffle_baseline(a.data(),b.data(),c.data(),old.data(),N);shuffle_candidate(a.data(),b.data(),c.data(),actual.data(),N);check("shuffle",N*16);}
 std::puts("PASS shuffle: every selector in every lane with arbitrary source vectors");
 // Recurrence stays within the Boolean-mask precondition at each step.
 for(const auto& test:extension_chains){
  const unsigned lanes=128/test.width, stride=test.width/8;
  for(unsigned trial=0;trial<128;++trial){
   for(unsigned lane=0;lane<lanes;++lane)put(a,lane*stride,test.width,random64()&1?~0ull:0);
   for(std::size_t i=0;i<N*lanes;++i)put(b,i*stride,test.width,random64()&1?~0ull:0);
   std::memcpy(expected.data(),a.data(),16);
   for(std::size_t i=0;i<N;++i){
    for(unsigned lane=0;lane<lanes/2;++lane){u64 mask=get(expected,2*lane*stride,test.width);put(c,2*lane*stride,test.width*2,mask?~0ull:0);}
    for(unsigned byte=0;byte<16;++byte)expected[byte]=c[byte]^b[i*16+byte];
   }
   test.fn[0](a.data(),b.data(),old.data(),N);test.fn[1](a.data(),b.data(),actual.data(),N);check(test.name,16);
  }
  std::printf("PASS %s: 128 recurrences of 1024 dependent operations\n",test.name);
 }
 if(argc>2&&!std::strcmp(argv[2],"validate"))return 0;
 fill_random(a);fill_random(b);fill_random(c);for(std::size_t i=0;i<N;++i){indices[i]=u32(random64());elements[i]=random64();}
 constexpr int repeats=8192;const double ops=double(repeats)*N;
 std::puts("case,variant,median_ns_per_operation,min,max; ten alternating rounds after two warmups");
 for(const auto& test:insertions)measure(test.name,[&](int j){for(int r=0;r<repeats;++r)test.fn[j](a.data(),elements.data(),indices.data(),actual.data(),N);},ops);
 for(const auto& test:extensions){for(std::size_t i=0;i<a.size();i+=test.width/8)put(a,i,test.width,random64()&1?~0ull:0);measure(test.name,[&](int j){for(int r=0;r<repeats;++r)test.fn[j](a.data(),actual.data(),N);},ops);}
 fill_random(a);
 for(const auto& test:shifts)measure(test.name,[&](int j){for(int r=0;r<repeats;++r)test.fn[j](a.data(),b.data(),actual.data(),N);},ops);
 measure("shuffle",[&](int j){auto fn=j?shuffle_candidate:shuffle_baseline;for(int r=0;r<repeats;++r)fn(a.data(),b.data(),c.data(),actual.data(),N);},ops);
 for(const auto& test:extension_chains){
  for(std::size_t i=0;i<a.size();i+=test.width/8){put(a,i,test.width,random64()&1?~0ull:0);put(b,i,test.width,random64()&1?~0ull:0);}
  measure(test.name,[&](int j){for(int r=0;r<repeats;++r)test.fn[j](a.data(),b.data(),actual.data(),N);},ops);
 }
}
