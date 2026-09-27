#include <array>
#include <cstddef>
#include <bit>
#include <cassert>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstring>
using u32=uint32_t;using u64=uint64_t;
struct alignas(16) state{u32 flags,pc;uint8_t unsavable;uint8_t pad[7];std::array<std::array<u32,4>,132>regs;};
static_assert(offsetof(state,regs)==16);
extern "C" void safe_checkpoint(state*,void*);
extern "C" void unsafe_checkpoint(state*,void*);
std::array<std::array<u32,4>,132>expected;bool unsafe=false,escape=false;unsigned calls=0;std::jmp_buf jump;
extern "C" void observe_checkpoint(state*s){++calls;assert(s->pc==0x1234);assert(bool(s->unsavable)==unsafe);if(!unsafe)assert(s->regs==expected);if(escape)std::longjmp(jump,1);}
int main(){
 alignas(32) std::array<std::array<u64,4>,132>input{};state s{};
 for(u32 i=0;i<132;i++)for(u32 lane=0;lane<4;lane++){
  if(i%3&&i<128&&i%2){auto bits=std::bit_cast<u64>((lane%2?-1.0:1.0)*(1+i+lane/4.0));input[i][lane]=bits;expected[i][lane]=u32((bits>>29)^0x40000000)&0x7fffffff;expected[i][lane]|=u32(bits>>32)&0x80000000;}
  else{auto value=(i+1)*100+lane;expected[i][lane]=value;std::memcpy(reinterpret_cast<char*>(input[i].data())+lane*4,&value,4);}
 }
 for(unsigned n=0;n<100;n++){
  s={};s.regs=expected;for(u32 i=0;i<132;i++)if(i%3){if(i<128)s.regs[i].fill(0xdeadbeef);else s.regs[i][0]=0xdeadbeef;}
  auto before=s;auto old_calls=calls;safe_checkpoint(&s,input.data());assert(!std::memcmp(&s,&before,sizeof(s))&&calls==old_calls);
  s.flags=1;escape=n%2;
  if(!setjmp(jump)){safe_checkpoint(&s,input.data());assert(!escape);}assert(s.regs==expected&&s.pc==0x1234&&!s.unsavable);
 }
 escape=false;unsafe=true;s.unsavable=0;unsafe_checkpoint(&s,input.data());assert(!s.unsavable&&calls==101);
 puts("SPU LLVM checkpoints: 132 registers, xfloat/scalar stores, 100 optimized ARM64 checkpoints, nonlocal escapes and unsafe-function deferral passed.");
}
