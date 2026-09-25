"""Lower actual production helpers for the next ARM64 SPU batch.

Original operations remain executable beside each candidate. Fail on unsupported
IRBuilder expressions. These fixtures execute operation semantics, not a whole
SPU program; the real integration is checked by the core build/device JIT map.
"""
from pathlib import Path
import importlib.util
import re
import sys

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('batch_ir', HERE / 'emit-spu-batch-fixture.py')
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
SOURCE = HERE.parents[1] / 'Emu/Cell/SPULLVMRecompiler.cpp'


class IR(base.IR):
    declarations = set()
    constants = {}

    def expr(self, text):
        text = text.strip()
        if m := re.fullmatch(r'm_ir->getInt64\((\w+)\)', text):
            return 'i64', str(self.constants.get(m[1], m[1]))
        if m := re.fullmatch(r'm_ir->CreateCall\(get_intrinsic<([^>]+)>\(llvm::Intrinsic::(\w+)\), \{(.*)\}\)', text):
            t = base.typ(m[1]); n, lane = base.vector_info(t)
            name = m[2].replace('aarch64_neon_', 'llvm.aarch64.neon.') + f'.v{n}{lane}'
            values = [self.expr(a) for a in base.args(m[3])]
            self.declarations.add(f'declare {t} @{name}(' + ', '.join(v[0] for v in values) + ')')
            return self.emit(t, f'call {t} @{name}(' + ', '.join(f'{vt} {v}' for vt, v in values) + ')')
        if m := re.fullmatch(r'm_ir->CreateZExtOrTrunc\((.*), get_type<([^>]+)>\(\)\)', text):
            st, value = self.expr(m[1]); t = base.typ(m[2])
            if st == t: return st, value
            op = 'zext' if int(st[1:]) < int(t[1:]) else 'trunc'
            return self.emit(t, f'{op} {st} {value} to {t}')
        if m := re.fullmatch(r'm_ir->CreateNeg\((.*)\)', text):
            t, value = self.expr(m[1]); zero = 'zeroinitializer' if t.startswith('<') else '0'
            return self.emit(t, f'sub {t} {zero}, {value}')
        if m := re.fullmatch(r'm_ir->Create(\w+)\((.*)\)', text):
            operations = {'Mul':'mul', 'UDiv':'udiv', 'URem':'urem', 'Add':'add', 'And':'and', 'Xor':'xor', 'Sub':'sub'}
            a = base.args(m[2])
            if m[1] in operations:
                lt, left = self.expr(a[0]); rt, right = self.expr(a[1])
                if rt == 'literal':
                    right = str(right) if not lt.startswith('<') else '<' + ', '.join([base.vector_info(lt)[1] + ' ' + str(right)] * base.vector_info(lt)[0]) + '>'
                    rt = lt
                assert lt == rt
                return self.emit(lt, f'{operations[m[1]]} {lt} {left}, {right}')
        return super().expr(text)


def insert_function(body, name, width, mask=False):
    n=128//width; t=f'<{n} x i{width}>'
    initial = '<' + ', '.join(f'i{width} {int.from_bytes(bytes(range(31-j*width//8,31-(j+1)*width//8,-1)),"little")}' for j in range(n)) + '>'
    element = {8:3,16:0x203,32:0x10203,64:0x1020304050607}[width]
    ir=IR({'vector':(t,initial if mask else '%vector'), 'element':(f'i{width}',str(element) if mask else '%element'),'index':('i32','%index')})
    rt,result=ir.body(body);assert rt==t
    return f'''define void @{name}(ptr %src, ptr %elements, ptr %indices, ptr %dst, i64 %count) {{
entry:
  %initial = load {t}, ptr %src, align 1
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %next, %loop ]
  %vector = phi {t} [ %initial, %entry ], [ {result}, %loop ]
  %ep = getelementptr i{width}, ptr %elements, i64 %i
  %ip = getelementptr i32, ptr %indices, i64 %i
  %element = load i{width}, ptr %ep, align 1
  %rawindex = load i32, ptr %ip, align 4
  %index = and i32 %rawindex, {n-1}
{chr(10).join(ir.lines)}
''' + (f'  %dp = getelementptr {t}, ptr %dst, i64 %i\n  store {t} {result}, ptr %dp, align 1\n' if mask else '') + f'''  %next = add i64 %i, 1
  %again = icmp ult i64 %next, %count
  br i1 %again, label %loop, label %end
end:
''' + ('' if mask else f'  store {t} {result}, ptr %dst, align 1\n') + '  ret void\n}\n'


def vectors(body, name, types, output, prep=''):
    ir=IR({key:(t,'%'+key) for key,t in types.items()});rt,result=ir.body(body);assert rt==output
    loads=[]
    for key,t in types.items():
        loads += [f'  %{key}p = getelementptr {t}, ptr %{key}src, i64 %i',f'  %{key} = load {t}, ptr %{key}p, align 1']
    return f'''define void @{name}(''' + ', '.join(f'ptr %{key}src' for key in types) + f''', ptr %dst, i64 %count) {{
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %next, %loop ]
{chr(10).join(loads)}
{chr(10).join(ir.lines)}
  %dp = getelementptr {output}, ptr %dst, i64 %i
  store {output} {result}, ptr %dp, align 1
  %next = add i64 %i, 1
  %again = icmp ult i64 %next, %count
  br i1 %again, label %loop, label %end
end:
  ret void
}}
'''


def extension_chain(body, name, width):
    t = f'<{128//width} x i{width}>'
    ir = IR({'val': (t, '%val')})
    result_type, result = ir.body(body)
    return f'''define void @{name}(ptr %src, ptr %masks, ptr %dst, i64 %count) {{
entry:
  %initial = load {t}, ptr %src, align 1
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %next, %loop ]
  %val = phi {t} [ %initial, %entry ], [ %feedback, %loop ]
{chr(10).join(ir.lines)}
  %narrow = bitcast {result_type} {result} to {t}
  %mp = getelementptr {t}, ptr %masks, i64 %i
  %mask = load {t}, ptr %mp, align 1
  %feedback = xor {t} %narrow, %mask
  %next = add i64 %i, 1
  %again = icmp ult i64 %next, %count
  br i1 %again, label %loop, label %end
end:
  store {t} %feedback, ptr %dst, align 1
  ret void
}}
'''


def main(output):
    source=SOURCE.read_text();functions=[]
    for width,name in [(8,'byte'),(16,'halfword'),(32,'word'),(64,'doubleword')]:
        new=base.body(source,'insert_spu_'+name)
        for mask in (False,True):
            if width==8 and not mask: continue  # Already measured in D-237.
            alias=('mask' if mask else 'insert')+str(width)
            functions += [insert_function('return m_ir->CreateInsertElement(vector, element, index);',alias+'_baseline',width,mask),insert_function(new,alias+'_candidate',width,mask)]
    for width in (8,16,32):
        t=f'<{128//width} x i{width}>';ot=f'<{64//width} x i{width*2}>'
        old=f'return m_ir->CreateAShr(m_ir->CreateShl(bitcast<u{width*2}[{64//width}]>(val), {width}), {width});'
        functions += [vectors(old,f'extend{width}_baseline',{'val':t},ot),vectors(base.body(source,f'extend_spu_boolean{width}'),f'extend{width}_candidate',{'val':t},ot)]
        functions += [extension_chain(old, f'extend_chain{width}_baseline', width), extension_chain(base.body(source, f'extend_spu_boolean{width}'), f'extend_chain{width}_candidate', width)]
    for width in (16,32):
        t=f'<{128//width} x i{width}>'
        prefix=f'const auto count = m_ir->CreateAnd(m_ir->CreateNeg(raw_count), {2*width-1});'
        old=prefix+f'return m_ir->CreateAShr(val, m_ir->CreateSelect(m_ir->CreateICmpSGT(count, splat<u{width}[{128//width}]>({width-1}).eval(m_ir)), splat<u{width}[{128//width}]>({width-1}).eval(m_ir), count));'
        new=prefix+base.body(source,f'spu_arithmetic_shift{width}')
        functions += [vectors(old,f'ashr{width}_baseline',{'val':t,'raw_count':t},t),vectors(new,f'ashr{width}_candidate',{'val':t,'raw_count':t},t)]
    t='<16 x i8>';new=base.body(source,'spu_constant_shuffle')
    # Use full dynamic SHUFB lowering as baseline; both inputs are arbitrary.
    prefix='const auto selectors = m_ir->CreateOr(raw, 128);'
    # Or is already supported by the shared lowerer.
    old=prefix+new.replace('return m_ir->CreateCall','const auto special = m_ir->CreateCall').rstrip()+'''
        const auto mapped = m_ir->CreateAnd(m_ir->CreateXor(selectors, 15), 159);
        return m_ir->CreateCall(get_intrinsic<u8[16]>(llvm::Intrinsic::aarch64_neon_tbx2), {special, a, b, mapped});'''
    functions += [vectors(old,'shuffle_baseline',{'raw':t,'a':t,'b':t},t),vectors(prefix+new,'shuffle_candidate',{'raw':t,'a':t,'b':t},t)]
    Path(output).write_text('\n'.join(sorted(IR.declarations))+'\n\n'+'\n'.join(functions))


if __name__=='__main__': main(sys.argv[1])
