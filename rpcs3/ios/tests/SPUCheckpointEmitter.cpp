#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <type_traits>
using u8=uint8_t;using u32=uint32_t;using f64=double;
#define ensure(x) assert(x)
#define FN(expr) [](auto& x){expr;}
#define ARCH_ARM64 1
struct spu_thread{u32 state,pc;u8 unsavable;};
struct emitter{
 llvm::LLVMContext&m_context;llvm::IRBuilder<>*m_ir;llvm::Function*m_function;llvm::Value*m_thread;
 llvm::Function*m_test_state;llvm::MDNode*m_md_likely=nullptr;
 static constexpr u32 s_reg_max=132;
 struct block{std::array<llvm::Value*,s_reg_max>reg{};std::array<u32,s_reg_max>store_context_ctr{};bool has_gpr_memory_barriers=false;}*m_block;
 struct info{bool fn;}*m_finfo;
 template<class T>llvm::Type*get_type(){if constexpr(std::is_array_v<T>)return llvm::FixedVectorType::get(get_type<std::remove_extent_t<T>>(),std::extent_v<T>);else if constexpr(std::is_same_v<T,double>)return m_ir->getDoubleTy();else return m_ir->getIntNTy(sizeof(T)*8);}
 template<class T>llvm::Value*spu_ptr(T spu_thread::*member){spu_thread dummy{};auto offset=reinterpret_cast<char*>(&(dummy.*member))-reinterpret_cast<char*>(&dummy);return m_ir->CreateConstGEP1_64(m_ir->getInt8Ty(),m_thread,offset);}
 template<class T>T*spu_context_attr(T*x){return x;}
 llvm::Value*init_reg_fixed(u32 i){return m_ir->CreateConstGEP1_64(m_ir->getInt8Ty(),m_thread,16+i*16);}
 llvm::Type*get_reg_type(u32 i){return i<128?get_type<u32[4]>():get_type<u32>();}
 llvm::Value*bitcast(llvm::Value*v,llvm::Type*t){return m_ir->CreateBitCast(v,t);}
 template<class T>llvm::Value*bitcast(llvm::Value*v){return bitcast(v,get_type<T>());}
 struct splat_value{llvm::Type*type;llvm::Value*eval(llvm::IRBuilder<>*){return llvm::Constant::getNullValue(type);}};
 template<class T>splat_value splat(u32 zero){assert(!zero);return {get_type<T>()};}
 void update_pc(u32 addr){spu_context_attr(m_ir->CreateStore(m_ir->getInt32(addr&0x3fffc),spu_ptr(&spu_thread::pc)))->setVolatile(true);}
#include "production.inc"
};
int main(){llvm::LLVMContext ctx;llvm::Module module("spu_checkpoint",ctx);llvm::IRBuilder<>ir(ctx);
 auto check=llvm::Function::Create(llvm::FunctionType::get(ir.getVoidTy(),{ir.getPtrTy()},false),llvm::Function::ExternalLinkage,"observe_checkpoint",module);
 for(bool true_function:{false,true}){
  auto fn=llvm::Function::Create(llvm::FunctionType::get(ir.getVoidTy(),{ir.getPtrTy(),ir.getPtrTy()},false),llvm::Function::ExternalLinkage,true_function?"unsafe_checkpoint":"safe_checkpoint",module);
  ir.SetInsertPoint(llvm::BasicBlock::Create(ctx,"entry",fn));emitter::block block;emitter::info info{true_function};emitter e{ctx,&ir,fn,fn->getArg(0),check,nullptr,&block,&info};
  for(u32 i=0;i<132;i++)if(i%3){auto type=i<128&&i%2?e.get_type<double[4]>():e.get_reg_type(i);block.reg[i]=ir.CreateLoad(type,ir.CreateConstGEP1_64(ir.getInt8Ty(),fn->getArg(1),i*32));}
  e.check_state(0x1234);ir.CreateRetVoid();
  if(!true_function){assert(block.has_gpr_memory_barriers);for(auto n:block.store_context_ctr)assert(n);}
 }
 if(llvm::verifyModule(module,&llvm::errs()))return 1;module.print(llvm::outs(),nullptr);
}
