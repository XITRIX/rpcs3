"""Frozen SPU compare/carry/borrow IR; the C++ fixture runs production passes."""
from pathlib import Path
import sys

def vec(bits, values):
    return '<' + ', '.join(f'i{bits} {v}' for v in values) + '>'

def generate(output):
    reverse = vec(8, range(15, -1, -1))
    def rev(value):
        return f'call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> {value}, <16 x i8> {reverse})'
    cases = []
    immediates = [-512, -511, -257, -256, -129, -128, -1, 0, 1, 37, 127, 128, 255, 256, 510, 511]
    for bits in (16, 32):
        for op, kind, opcode in [('sgt', 0, 'CGTH' if bits == 16 else 'CGT'), ('ugt', 1, 'CLGTH' if bits == 16 else 'CLGT')]:
            cases.append((opcode, bits, kind, [], 0, False))
            for imm in immediates:
                cases.append((opcode+'I_'+str(imm).replace('-', 'm'), bits, kind, [imm] * (128 // bits), 0, False))
            # Nonuniform constants catch a missing constant-lane reversal.
            cases.append((opcode+'_constants', bits, kind, [0x1387 + i * 0x3723 for i in range(128 // bits)], 0, False))
    cases += [('BG', 32, 2, [], 0, False), ('CG', 32, 3, [], 0, False)]
    for shared in range(1, 7):
        cases.append(('guard_shared_'+str(shared), 32, 3 if shared == 6 else 0, [], shared, False))
    cases += [('guard_plain_input', 32, 0, [], 0, True),
              ('guard_add_output', 32, 4, [], 0, False),
              ('guard_equal', 32, 5, [], 0, False),
              ('guard_zero_ordering', 32, 6, [], 0, False),
              ('guard_halfword_borrow', 16, 2, [], 0, False)]
    functions, tests = [], []
    for name, bits, kind, constants, shared, plain in cases:
        n = 128 // bits
        ty, pred = f'<{n} x i{bits}>', f'<{n} x i1>'
        for chain in range(3 if not constants else 2):
            label = name + ('', '_chain', '_chain_b')[chain]
            body = ''
            for arg in ('a', 'b'):
                if arg == 'b' and constants:
                    continue
                body += f'%{arg}r = '+(f'xor <16 x i8> %{arg}, zeroinitializer' if plain else rev('%'+arg))+'\n'
                body += f'%{arg}w = bitcast <16 x i8> %{arg}r to {ty}\n'
            rhs = vec(bits, constants) if constants else '%bw'
            if kind == 3:
                body += f'%sum = add {ty} %aw, {rhs}\n%cmp = icmp ult {ty} %sum, %aw\n'
            elif kind == 4:
                body += f'%value = add {ty} %aw, {rhs}\n'
            else:
                predicate = {0: 'sgt', 1: 'ugt', 2: 'ule', 5: 'eq', 6: 'sgt'}[kind]
                body += f'%cmp = icmp {predicate} {ty} %aw, {rhs}\n'
            if kind != 4:
                body += f'%value = '+('zext' if kind in (2,3,6) else 'sext')+f' {pred} %cmp to {ty}\n'
            body += f'%bytes = bitcast {ty} %value to <16 x i8>\n'
            body += ('%r' if shared else '%out')+' = '+rev('%bytes')+'\n'
            if shared:
                source = '%ar'
                if shared == 2:
                    body += f'%visible = bitcast {ty} %aw to <16 x i8>\n'
                    source = '%visible'
                elif shared == 3:
                    body += f'%condition = zext {pred} %cmp to {ty}\n%visible = bitcast {ty} %condition to <16 x i8>\n'
                    source = '%visible'
                elif shared == 4:
                    body += f'%visible = bitcast {ty} %value to <16 x i8>\n'
                    source = '%visible'
                elif shared == 5:
                    source = '%bytes'
                elif shared == 6:
                    body += f'%visible = bitcast {ty} %sum to <16 x i8>\n'
                    source = '%visible'
                body += f'%out = xor <16 x i8> %r, {source}\n'
            s = f'define void @{label}_baseline(ptr %asrc, ptr %bsrc, ptr %csrc, ptr %dst, i64 %n) {{\nentry:\n br label %loop\nloop:\n %i = phi i64 [0, %entry], [%next, %loop]\n'
            if chain:
                s += ' %state = phi <16 x i8> [zeroinitializer, %entry], [%out, %loop]\n'
            for k, arg in enumerate('abc', 1):
                loaded = f'%raw{arg}' if k == chain else '%'+arg
                s += f' %{arg}p = getelementptr <16 x i8>, ptr %{arg}src, i64 %i\n {loaded} = load <16 x i8>, ptr %{arg}p, align 1\n'
                if k == chain:
                    s += f' %{arg} = xor <16 x i8> {loaded}, %state\n'
            s += ' '+body.replace('\n', '\n ')+'\n'
            s += ' %dp = getelementptr <16 x i8>, ptr %dst, i64 %i\n store <16 x i8> %out, ptr %dp, align 1\n %next = add i64 %i, 1\n %again = icmp ult i64 %next, %n\n br i1 %again, label %loop, label %end, !llvm.loop !0\nend:\n ret void\n}\n'
            functions.append(s)
            tests.append((label, bits, kind, constants, shared, plain, chain))
    output.write_text('declare <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8>, <16 x i8>)\n'+'\n'.join(functions)+'\n!0 = distinct !{!0, !1}\n!1 = !{!"llvm.loop.unroll.disable"}\n')
    h = 'using fn=void(*)(const void*,const void*,const void*,void*,unsigned long long);\n'
    for name, *_ in tests:
        for version in ('baseline', 'candidate'):
            h += f'extern "C" void {name}_{version}(const void*,const void*,const void*,void*,unsigned long long);\n'
    h += 'struct test {const char* name; unsigned bits, kind, shared, plain, chain; bool immediate; std::array<std::uint32_t,8> constants; fn functions[2];};\nconst test tests[]={\n'
    for name, bits, kind, constants, shared, plain, chain in tests:
        cs = ','.join(str(x & ((1 << bits) - 1))+'u' for x in constants)
        h += f'{{"{name}",{bits},{kind},{shared},{int(plain)},{chain},{str(bool(constants)).lower()},{{{cs}}},{{{name}_baseline,{name}_candidate}}}},\n'
    output.with_suffix('.h').write_text(h+'};\n')
    print(len(tests), 'production comparison fixtures')

if __name__ == '__main__':
    generate(Path(sys.argv[1]))
