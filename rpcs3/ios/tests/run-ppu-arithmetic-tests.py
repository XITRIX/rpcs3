#!/usr/bin/env python3
"""Execute extracted PPU LLVM opcode bodies on ARM64 against integer/IEEE oracles.

Only register plumbing is mocked. The opcode bodies, XER cases, flag helpers and
CR0 update come from production. Requires a native LLVM 20+ development install.
"""
import argparse
import ctypes
import math
import os
from pathlib import Path
import platform
import random
import re
import shlex
import shutil
import struct
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
CORE = HERE.parents[1]
OPS = 'SUBFC ADDC SUBF NEG SUBFE ADDE ADDZE SUBFZE SUBFME MULLD ADDME MULLW DIVDU DIVWU DIVD DIVW ADD'.split()
parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=CORE/'Emu/Cell/PPUTranslator.cpp')
parser.add_argument('--negative-control', choices=['xer', 'nan', 'overflow'])
args = parser.parse_args()
src = args.source.read_text()

def extract(name):
    match = re.search(r'^(?:void|Value\*) PPUTranslator::'+name+r'\([^\n]*\)\n\{', src, re.M)
    if not match:
        raise RuntimeError('Missing production method '+name)
    start = match.start()
    brace = src.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (src[end] == '{') - (src[end] == '}')
        end += 1
    return src[start:end]

methods = [extract(x) for x in OPS + ['FCTIW','FCTIWZ','FCTID','FCTIDZ','GetGpr','SetGpr','GetFpr','SetFpr','SetCrField','SetCrFieldSignedCmp','GetCarry','SetCarry','SetOverflow','AddOverflow']]
for name, prefix, suffix in [('MFSPR','Value* result;','SetGpr(op.rd, result);'), ('MTSPR','const auto value = GetGpr(op.rs);','')]:
    body = extract(name).split('case 0x001:')[1].split('break;')[0]
    methods.append('void PPUTranslator::'+name+'(ppu_opcode_t op) {\n'+prefix+'\n'+body+'\n'+suffix+'\n}')
code = '\n'.join(methods)
if args.negative_control == 'xer':
    code = code.replace('RegLoad(m_so), GetType<u64>()), 31', 'RegLoad(m_so), GetType<u64>()), 29').replace('RegLoad(m_ca), GetType<u64>()), 29', 'RegLoad(m_ca), GetType<u64>()), 31')
if args.negative_control == 'nan':
    code = re.sub(r'm_ir->CreateSelect\(m_ir->CreateFCmpUNO\(b, b\), m_ir->getInt(?:32|64)\(INT(?:32|64)_MIN\),\s*(Call\([^;]+\))\)', r'\1', code)
if args.negative_control == 'overflow':
    code = code.replace('RegStore(m_ir->CreateOr(RegLoad(m_so), bit), m_so);', 'RegStore(bit, m_so);')

fixture = r'''
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <cstdint>
#include <climits>
#include <type_traits>
using namespace llvm;
using u8=uint8_t; using u32=uint32_t; using u64=uint64_t;
using s32=int32_t; using s64=int64_t; using f32=float; using f64=double;
constexpr s32 smin=INT32_MIN;
struct ppu_opcode_t { u32 ra=0, rb=1, rd=2, rs=0, frb=0, frd=1; bool oe=false,rc=false; };
struct reg { Value* ptr; Type* type; };
struct PPUTranslator {
 LLVMContext& ctx; Module& module; IRBuilder<>* m_ir;
 reg m_gpr[3],m_fpr[2],m_ca,m_ov,m_so,m_cnt,m_cr[4];
 template<class T> Type* GetType() {
  if constexpr(std::is_same_v<T,double>) return m_ir->getDoubleTy();
  if constexpr(std::is_same_v<T,float>) return m_ir->getFloatTy();
  return m_ir->getIntNTy(std::is_same_v<T,bool>?1:sizeof(T)*8);
 }
 template<class T> Value* GetUndef() { return UndefValue::get(GetType<T>()); }
 Value* Trunc(Value* v,Type* t) { return m_ir->CreateTruncOrBitCast(v,t); }
 Value* ZExt(Value* v,Type* t=nullptr) { return m_ir->CreateZExtOrBitCast(v,t?t:GetType<u64>()); }
 Value* SExt(Value* v,Type* t=nullptr) { return m_ir->CreateSExtOrBitCast(v,t?t:GetType<u64>()); }
 Value* bitcast(Value* v,Type* t) { return m_ir->CreateBitCast(v,t); }
 Value* RegLoad(reg r) { auto v=m_ir->CreateLoad(GetType<u64>(),r.ptr); return r.type->isDoubleTy()?bitcast(v,r.type):Trunc(v,r.type); }
 void RegStore(Value* v,reg r) { m_ir->CreateStore(v->getType()->isDoubleTy()?bitcast(v,GetType<u64>()):ZExt(v),r.ptr); }
 Value* IsZero(Value* v) { return m_ir->CreateICmpEQ(v,ConstantInt::get(v->getType(),0)); }
 Value* IsNotZero(Value* v) { return m_ir->CreateNot(IsZero(v)); }
 Value* IsOnes(Value* v) { return m_ir->CreateICmpEQ(v,ConstantInt::getSigned(v->getType(),-1)); }
 Value* Call(Type* t,const char* name,Value* v) { return m_ir->CreateCall(module.getOrInsertFunction(name,FunctionType::get(t,{v->getType()},false)),{v}); }
 void SetCrb(u32 n,Value* v) { RegStore(v,m_cr[n]); }
 void UNK(ppu_opcode_t) { llvm_unreachable("unexpected fallback"); }
 Value* GetGpr(u32,u32=64); void SetGpr(u32,Value*);
 Value* GetFpr(u32,u32=64,bool=false); void SetFpr(u32,Value*);
 void SetCrField(u32,Value*,Value*,Value*,Value* = nullptr);
 void SetCrFieldSignedCmp(u32,Value*,Value*);
 Value* GetCarry(); void SetCarry(Value*); void SetOverflow(Value*);
 Value* AddOverflow(Value*,Value*,Value*);
 DECLARATIONS
 void init(Value* state) {
  auto r=[&](unsigned i,Type* t) { return reg{m_ir->CreateConstGEP1_64(GetType<u64>(),state,i),t}; };
  for(unsigned i=0;i<3;i++) m_gpr[i]=r(i,GetType<u64>());
  m_ca=r(3,GetType<bool>()); m_ov=r(4,GetType<bool>()); m_so=r(5,GetType<bool>()); m_cnt=r(6,GetType<u8>());
  for(unsigned i=0;i<4;i++) m_cr[i]=r(i+7,GetType<bool>());
  m_fpr[0]=r(11,GetType<double>()); m_fpr[1]=r(12,GetType<double>());
 }
};
#define ARCH_ARM64 1
#include "production.inc"
int main() {
 LLVMContext ctx; Module module("ppu_arithmetic",ctx); IRBuilder<> ir(ctx);
 auto emit=[&](const char* name,auto body) {
  auto fn=Function::Create(FunctionType::get(ir.getVoidTy(),{ir.getPtrTy()},false),Function::ExternalLinkage,name,module);
  ir.SetInsertPoint(BasicBlock::Create(ctx,"entry",fn));
  PPUTranslator p{ctx,module,&ir}; p.init(fn->getArg(0)); body(p); ir.CreateRetVoid();
 };
 EMIT
 if(verifyModule(module,&errs())) return 1;
 module.print(outs(),nullptr);
}
'''
allops = OPS+['FCTIW','FCTIWZ','FCTID','FCTIDZ','MFSPR','MTSPR']
fixture=fixture.replace('DECLARATIONS','\n'.join('void '+x+'(ppu_opcode_t);' for x in allops))
emit=[]
for op in allops:
    for flags in range(4):
        emit.append('emit("'+op+str(flags)+'", [](auto& p) { ppu_opcode_t op; op.oe='+str(flags&1)+'; op.rc='+str(flags>>1)+'; p.'+op+'(op); });')
emit.append('emit("carry_read", [](auto& p) { ppu_opcode_t op; p.ADDC(op); p.MFSPR(op); });')
emit.append('emit("write_carry", [](auto& p) { ppu_opcode_t op; p.MTSPR(op); p.SetGpr(0,p.m_ir->getInt64(0)); p.ADDZE(op); });')
fixture=fixture.replace('EMIT','\n'.join(emit))
config=os.environ.get('LLVM_CONFIG') or shutil.which('llvm-config')
if not config: raise SystemExit('Set LLVM_CONFIG to a native LLVM 20+ llvm-config.')
def llvm(*a): return subprocess.check_output([config,*a],text=True).strip()
def run(a,**kw): return subprocess.run(list(map(str,a)),check=True,**kw)
if platform.machine() not in ('arm64','aarch64'): raise SystemExit('ARM64 host required')
with tempfile.TemporaryDirectory(prefix='ppu-arithmetic-') as tmp:
    out=Path(tmp);(out/'production.inc').write_text(code);(out/'fixture.cpp').write_text(fixture)
    cc=shlex.split(os.environ.get('CXX','clang++'))
    run([*cc,*shlex.split(llvm('--cxxflags')),'-std=c++20','-O2','-I',out,out/'fixture.cpp',*shlex.split(llvm('--ldflags','--libs','all','--system-libs')),'-Wl,-rpath,'+llvm('--libdir'),'-o',out/'emit'])
    with (out/'fixture.ll').open('w') as f: run([out/'emit'],stdout=f)
    target='arm64-apple-macos15' if platform.system()=='Darwin' else 'aarch64-unknown-linux-gnu'
    run([Path(llvm('--bindir'))/'llc','-O2','-mtriple='+target,'-mcpu=generic','-filetype=obj',out/'fixture.ll','-o',out/'fixture.o'])
    run([*cc,'-dynamiclib' if platform.system()=='Darwin' else '-shared',out/'fixture.o','-o',out/'fixture.dylib'])
    lib=ctypes.CDLL(str(out/'fixture.dylib'))
    mask=(1<<64)-1
    signed=lambda x,bits=64: (x & ((1<<bits)-1))-(1<<bits) if x & (1<<(bits-1)) else x & ((1<<bits)-1)
    rng=random.Random(0x505055)
    edges=[0,1,2,mask,mask-1,1<<63,(1<<63)-1,(1<<63)+1,0x7fffffff,0x80000000,0xffffffff,0x100000000]
    pairs=[(a,b) for a in edges for b in edges]+[(rng.getrandbits(64),rng.getrandbits(64)) for _ in range(300)]
    count=0
    def execute(name,state):
        global count
        value=(ctypes.c_uint64*13)(*state)
        getattr(lib,name)(value);count+=1
        return list(value)
    for op in OPS:
        for flags in range(4):
            for a,b in pairs:
                for initial in range(8):
                    ca,ov,so=initial&1,(initial>>1)&1,initial>>2
                    state=[a,b,0,ca,ov,so,0x5a,1,0,1,0,0,0]
                    A,B=signed(a),signed(b);carry=ca
                    if op in ('SUBFC','SUBF'): wide=B-A; unsigned=b+((~a)&mask)+1
                    elif op in ('ADDC','ADD'): wide=A+B; unsigned=a+b
                    elif op=='NEG': wide=-A; unsigned=0
                    elif op=='SUBFE': wide=B-A-1+ca; unsigned=b+((~a)&mask)+ca
                    elif op=='ADDE': wide=A+B+ca; unsigned=a+b+ca
                    elif op=='ADDZE': wide=A+ca; unsigned=a+ca
                    elif op=='SUBFZE': wide=-A-1+ca; unsigned=((~a)&mask)+ca
                    elif op=='SUBFME': wide=-A-2+ca; unsigned=((~a)&mask)+mask+ca
                    elif op=='ADDME': wide=A-1+ca; unsigned=a+mask+ca
                    elif op=='MULLD': wide=A*B; unsigned=0
                    elif op=='MULLW': wide=signed(a,32)*signed(b,32); unsigned=0
                    else:
                        bits=32 if op.endswith('W') or op=='DIVWU' else 64
                        is_signed=op in ('DIVD','DIVW')
                        x=signed(a,bits) if is_signed else a&((1<<bits)-1)
                        y=signed(b,bits) if is_signed else b&((1<<bits)-1)
                        overflow=y==0 or (is_signed and x==-(1<<(bits-1)) and y==-1)
                        wide=0 if overflow else (abs(x)//abs(y))*(-1 if (x<0)!=(y<0) else 1)
                        unsigned=0
                    if op.startswith('DIV'): pass
                    elif op=='MULLW': overflow=not(-(1<<31)<=wide<(1<<31))
                    else: overflow=not(-(1<<63)<=wide<(1<<63))
                    if op in ('SUBFC','ADDC','SUBFE','ADDE','ADDZE','SUBFZE','SUBFME','ADDME'): carry=int(unsigned>mask)
                    result=wide&mask
                    if op=='DIVW': result=wide&0xffffffff # translator zero-extends the 32-bit result
                    expected=state.copy();expected[2]=result;expected[3]=carry
                    if flags&1: expected[4]=int(overflow);expected[5]=so|int(overflow)
                    if flags&2: expected[7:11]=[int(signed(result)<0),int(signed(result)>0),int(result==0),expected[5]]
                    actual=execute(op+str(flags),state)
                    assert actual==expected,(op,flags,hex(a),hex(b),initial,actual,expected)
    for flags in range(8):
        ca,ov,so=flags&1,(flags>>1)&1,flags>>2
        for cnt in range(128):
            xer=(so<<31)|(ov<<30)|(ca<<29)|cnt
            state=[0,0,0,ca,ov,so,cnt,0,0,0,0,0,0]
            assert execute('MFSPR0',state)[2]==xer
            state=[xer,0,0,0,0,0,0,0,0,0,0,0,0]
            assert execute('MTSPR0',state)[3:7]==[ca,ov,so,cnt]
            assert execute('write_carry',state)[2]==ca
    assert execute('carry_read',[mask,1,0,0,0,0,0,0,0,0,0,0,0])[2]==1<<29
    bits=[0x7ff0000000000001,0xfff0000000000001,0x7ff8000000000000,0xfff8000000000000,0x7fffffffffffffff,0xffffffffffffffff,0,1,1<<63,0x7ff0000000000000,0xfff0000000000000]
    values=[-.5,.5,1.5,2.5,-1.5,-2.5,1.9,-1.9]
    for width in (32,64):
        for x in (-math.ldexp(1.0,width-1),math.ldexp(1.0,width-1)):
            values += [x,math.nextafter(x,-math.inf),math.nextafter(x,math.inf)]
    bits += [struct.unpack('Q',struct.pack('d',x))[0] for x in values]+[rng.getrandbits(64) for _ in range(3000)]
    for op in ['FCTIW','FCTIWZ','FCTID','FCTIDZ']:
        width=32 if 'W' in op else 64;lo=-(1<<(width-1));hi=(1<<(width-1))-1
        for bit in bits:
            x=struct.unpack('d',struct.pack('Q',bit))[0]
            # Classify NaN using bits, including signaling NaNs, before arithmetic.
            if bit&0x7ff0000000000000==0x7ff0000000000000 and bit&((1<<52)-1): expected=lo
            elif x<=lo: expected=lo
            elif x>=hi: expected=hi
            else: expected=math.trunc(x) if op.endswith('Z') else round(x)
            state=[0]*13;state[11]=bit
            assert execute(op+'0',state)[12]==expected&mask,(op,hex(bit),expected)
    print(f'PPU arithmetic: {count} generated ARM64 cases passed (LLVM {llvm("--version")}).')
