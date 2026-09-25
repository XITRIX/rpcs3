"""Execute production helpers beside frozen D-238 lowering and scalar references.

This small IRBuilder translator fails on unsupported expressions. Integration
and the real device LLVM backend are separately checked by build/JIT capture.
"""
from pathlib import Path
import importlib.util
import re
import sys

HERE = Path(__file__).resolve().parent
SOURCE = HERE.parents[1] / 'Emu/Cell/SPULLVMRecompiler.cpp'
spec = importlib.util.spec_from_file_location('base', HERE / 'emit-spu-batch-fixture.py')
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)

class IR(base.IR):
 declarations=set()
 def expr(self,s):
  s=s.strip()
  if m:=re.fullmatch(r'm_ir->CreateCall\(get_intrinsic<([^>]+)>\(llvm::Intrinsic::(\w+)\), \{(.*)\}\)',s):
   t=base.typ(m[1]);n,lane=base.vector_info(t);suffix='f64' if lane=='double' else 'f32' if lane=='float' else lane
   intrinsic=m[2].replace('aarch64_neon_','aarch64.neon.')
   name='llvm.'+intrinsic+f'.v{n}{suffix}'
   vals=[self.expr(a) for a in base.args(m[3])];self.declarations.add(f'declare {t} @{name}('+', '.join(v[0] for v in vals)+')')
   return self.emit(t,f'call {t} @{name}('+', '.join(f'{ty} {v}' for ty,v in vals)+')')
  if m:=re.fullmatch(r'm_ir->CreateNeg\((.*)\)',s):
   t,v=self.expr(m[1]);return self.emit(t,f'sub {t} zeroinitializer, {v}')
  if m:=re.fullmatch(r'm_ir->Create(Mul|Sub)\((.*)\)',s):
   args=base.args(m[2]);t,a=self.expr(args[0]);bt,b=self.expr(args[1])
   if bt=='literal':n,l=base.vector_info(t);b='<'+', '.join([f'{l} {b}']*n)+'>';bt=t
   assert t==bt
   return self.emit(t,f'{m[1].lower()} {t} {a}, {b}')
  return super().expr(s)

BASELINES = {'rotate16': {'old': 'return m_ir->CreateCall(get_intrinsic<u16[8]>(llvm::Intrinsic::fshl), {a, a, b});',
              'types': ['<8 x i16>', '<8 x i16>', '<8 x i16>'],
              'output': '<8 x i16>'},
 'rotate32': {'old': 'return m_ir->CreateCall(get_intrinsic<u32[4]>(llvm::Intrinsic::fshl), {a, a, b});',
              'types': ['<4 x i32>', '<4 x i32>', '<4 x i32>'],
              'output': '<4 x i32>'},
 'quadrot': {'old': 'const auto count = m_ir->CreateAnd(m_ir->CreateShuffleVector(b, b, {3, 3, 3, 3}), 7);\n'
                    'const auto neighbor = m_ir->CreateShuffleVector(a, splat<u32[4]>(0).eval(m_ir), {3, 0, '
                    '1, 2});\n'
                    'return m_ir->CreateCall(get_intrinsic<u32[4]>(llvm::Intrinsic::fshl), {a, neighbor, '
                    'count});',
             'types': ['<4 x i32>', '<4 x i32>', '<4 x i32>'],
             'output': '<4 x i32>'},
 'quadleft': {'old': 'const auto count = m_ir->CreateAnd(m_ir->CreateShuffleVector(b, b, {3, 3, 3, 3}), 7);\n'
                     'const auto neighbor = m_ir->CreateShuffleVector(a, splat<u32[4]>(0).eval(m_ir), {4, 0, '
                     '1, 2});\n'
                     'return m_ir->CreateCall(get_intrinsic<u32[4]>(llvm::Intrinsic::fshl), {a, neighbor, '
                     'count});',
              'types': ['<4 x i32>', '<4 x i32>', '<4 x i32>'],
              'output': '<4 x i32>'},
 'quadright': {'old': 'const auto count = m_ir->CreateAnd(m_ir->CreateShuffleVector(b, b, {3, 3, 3, 3}), '
                      '7);\n'
                      'const auto neighbor = m_ir->CreateShuffleVector(a, splat<u32[4]>(0).eval(m_ir), {1, '
                      '2, 3, 4});\n'
                      'return m_ir->CreateCall(get_intrinsic<u32[4]>(llvm::Intrinsic::fshr), {neighbor, a, '
                      'count});',
               'types': ['<4 x i32>', '<4 x i32>', '<4 x i32>'],
               'output': '<4 x i32>'},
 'cgx': {'old': 'const auto x = m_ir->CreateAShr(m_ir->CreateShl(c, 31), 31);\n'
                'const auto sum = m_ir->CreateAdd(a, b);\n'
                'const auto carry = m_ir->CreateSExt(m_ir->CreateICmpULT(sum, a), get_type<u32[4]>());\n'
                'const auto boundary = m_ir->CreateAnd(m_ir->CreateSExt(m_ir->CreateICmpEQ(sum, x), '
                'get_type<u32[4]>()), x);\n'
                'return m_ir->CreateLShr(m_ir->CreateOr(carry, boundary), 31);',
         'types': ['<4 x i32>', '<4 x i32>', '<4 x i32>'],
         'output': '<4 x i32>'},
 'bgx': {'old': 'const auto mask = m_ir->CreateShl(c, 31);\n'
                'const auto gt = m_ir->CreateSExt(m_ir->CreateICmpUGT(b, a), get_type<u32[4]>());\n'
                'const auto eq = m_ir->CreateSExt(m_ir->CreateICmpEQ(a, b), get_type<u32[4]>());\n'
                'return m_ir->CreateLShr(m_ir->CreateOr(gt, m_ir->CreateAnd(eq, mask)), 31);',
         'types': ['<4 x i32>', '<4 x i32>', '<4 x i32>'],
         'output': '<4 x i32>'},
 'compare_gt': {'old': '\n'
                       '\t\treturn m_ir->CreateSExt(m_ir->CreateICmpSGT(spu_xfloat_order_key(a), '
                       'spu_xfloat_order_key(b)), get_type<u32[4]>());\n'
                       '\t',
                'types': ['<4 x i32>', '<4 x i32>', '<4 x i32>'],
                'output': '<4 x i32>'},
 'compare_eq': {'old': '\n'
                       '\t\treturn m_ir->CreateSExt(m_ir->CreateICmpEQ(canonical_spu_xfloat(a), '
                       'canonical_spu_xfloat(b)), get_type<u32[4]>());\n'
                       '\t',
                'types': ['<4 x i32>', '<4 x i32>', '<4 x i32>'],
                'output': '<4 x i32>'},
 'compare_mgt': {'old': '\n'
                        '\t\treturn '
                        'm_ir->CreateSExt(m_ir->CreateICmpUGT(canonical_spu_xfloat(m_ir->CreateAnd(a, '
                        '0x7fffffff)), canonical_spu_xfloat(m_ir->CreateAnd(b, 0x7fffffff))), '
                        'get_type<u32[4]>());\n'
                        '\t',
                 'types': ['<4 x i32>', '<4 x i32>', '<4 x i32>'],
                 'output': '<4 x i32>'},
 'byterot': {'old': 'const auto count = m_ir->CreateAnd(b, 15);\n'
                    'const auto base = build<u8[16]>(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, '
                    '15).eval(m_ir);\n'
                    'const auto shuffled = '
                    'm_ir->CreateCall(get_intrinsic<u8[16]>(llvm::Intrinsic::aarch64_neon_tbl1), {a, '
                    'm_ir->CreateAnd(m_ir->CreateSub(base, count), 15)});\n'
                    'return shuffled;',
             'types': ['<16 x i8>', '<16 x i8>', '<16 x i8>'],
             'output': '<16 x i8>'},
 'byteleft': {'old': 'const auto count = m_ir->CreateAnd(b, 31);\n'
                     'const auto base = build<u8[16]>(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, '
                     '15).eval(m_ir);\n'
                     'const auto shuffled = '
                     'm_ir->CreateCall(get_intrinsic<u8[16]>(llvm::Intrinsic::aarch64_neon_tbl1), {a, '
                     'm_ir->CreateSub(base, count)});\n'
                     'return shuffled;',
              'types': ['<16 x i8>', '<16 x i8>', '<16 x i8>'],
              'output': '<16 x i8>'},
 'byteright': {'old': 'const auto count = m_ir->CreateAnd(m_ir->CreateNeg(b), 31);\n'
                      'const auto base = build<u8[16]>(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, '
                      '15).eval(m_ir);\n'
                      'const auto shuffled = '
                      'm_ir->CreateCall(get_intrinsic<u8[16]>(llvm::Intrinsic::aarch64_neon_tbl1), {a, '
                      'm_ir->CreateAdd(base, count)});\n'
                      'return shuffled;',
               'types': ['<16 x i8>', '<16 x i8>', '<16 x i8>'],
               'output': '<16 x i8>'}}

def function(op,variant):
 values={key:(t,'%'+key) for key,t in zip(('a','b','c'),op['types'])};
 if op['imm'] >= 0: values['b']=('<16 x i8>', '<'+', '.join(['i8 '+str(op['imm'])]*16)+'>')
 ir=IR(values);rt,result=ir.body(op['old' if variant=='baseline' else 'new']);assert rt==op['output']
 loads='\n'.join(f'  %{k}p = getelementptr {t}, ptr %{k}src, i64 %i\n  %{k} = load {t}, ptr %{k}p, align 1' for k,t in zip(('a','b','c'),op['types']))
 text = f'''define void @{op['name']}_{variant}(ptr %asrc, ptr %bsrc, ptr %csrc, ptr %dst, i64 %count) {{
entry:
  br label %loop
loop:
  %i = phi i64 [0, %entry], [%next, %loop]
{loads}
{chr(10).join(ir.lines)}
  %dp = getelementptr {rt}, ptr %dst, i64 %i
  store {rt} {result}, ptr %dp, align 1
  %next = add i64 %i, 1
  %again = icmp ult i64 %next, %count
  br i1 %again, label %loop, label %end, !llvm.loop !0
end:
  ret void
}}
'''
 if op.get('chain'):
  text=text.replace('  %a = load ', '  %raw_a = load ')
  t=op['types'][0]
  text=text.replace('  %ap = getelementptr', f'  %state = phi {t} [zeroinitializer, %entry], [{result}, %loop]\n  %ap = getelementptr')
  text=text.replace('  %bp = getelementptr', f'  %a = xor {t} %raw_a, %state\n  %bp = getelementptr')
 if op['imm'] < 0: text=text.replace(', !llvm.loop !0', '')
 return text


def main(output):
 source = SOURCE.read_text()
 IR.helpers = {name: (['val'], base.body(source, name)) for name in ('canonical_spu_xfloat', 'spu_xfloat_order_key')}
 ops = []
 for name, spec in BASELINES.items():
  helper = 'compare_spu_xfloat_' + name[8:] if name.startswith('compare_') else 'spu_' + name
  spec = dict(spec, new=base.body(source, helper), name=name, imm=-1)
  if name.startswith('byte'):
   for immediate in range(128):
    for chain in (False, True):
     ops.append(dict(spec, name=name+str(immediate)+('_chain' if chain else ''), imm=immediate, chain=chain))
  else: ops.append(spec)
 functions = [function(op, v) for op in ops for v in ('baseline', 'candidate')]
 output.write_text('\n'.join(sorted(IR.declarations))+'\n'+'\n'.join(functions)+'\n!0 = distinct !{!0, !1}\n!1 = !{!"llvm.loop.unroll.disable"}\n')
 header = 'using fn=void(*)(const void*,const void*,const void*,void*,unsigned long long);\n'
 for op in ops:
  for variant in ('baseline', 'candidate'):
   header += f'extern "C" void {op["name"]}_{variant}(const void*,const void*,const void*,void*,unsigned long long);\n'
 header += 'struct test { const char* name; int bytes; int imm; bool chain; fn functions[2]; };\nconst test tests[]={\n'
 for op in ops:
  header += f'{{"{op["name"]}",16,{op["imm"]},{str(op.get("chain",False)).lower()},{{{op["name"]}_baseline,{op["name"]}_candidate}}}},\n'
 header += '};\n'
 output.with_suffix('.h').write_text(header)
 print(f'Generated {len(ops)} production-helper pairs (13 semantic paths, every encoded byte immediate).')

if __name__ == '__main__': main(Path(sys.argv[1]))
