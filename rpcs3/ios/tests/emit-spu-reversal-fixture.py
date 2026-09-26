"""Baseline IR shapes from SPU lowering; candidates are produced by the real pass.

No candidate algorithm is restated here. SPUARM64LoweringFixture clones these
functions and applies the production header, then the production pass sequence.
"""
from pathlib import Path
import sys


def vec(width, values):
    return '<' + ', '.join(f'i{width} {v}' for v in values) + '>'


def generate(output):
    reverse = vec(8, range(15, -1, -1))
    tbl = '@llvm.aarch64.neon.tbl1.v16i8'
    def call(data, index=reverse):
        return f'call <16 x i8> {tbl}(<16 x i8> {data}, <16 x i8> {index})'
    cases = []
    for width in (8, 16, 32, 64):
        size, lanes = width // 8, 128 // width
        ty = f'<{lanes} x i{width}>'
        preferred = (12 if width < 64 else 8) // size
        cast = (f'%aw = bitcast <16 x i8> %ar to {ty}\n%bw = bitcast <16 x i8> %b to {ty}\n'
                if width != 8 else '')
        aw, bw = ('%aw', '%bw') if width != 8 else ('%ar', '%b')
        body = f'%ar = {call("%a")}\n' + cast
        body += '%ci = extractelement <16 x i8> %c, i32 12\n'
        body += f'%idx = and i8 %ci, {lanes - 1}\n'
        if width != 8:
            body += f'%ix = zext i8 %idx to i{width}\n'
        body += f'%ii = insertelement {ty} poison, i{width} '+('%idx' if width == 8 else '%ix')+', i32 0\n'
        body += f'%indices = shufflevector {ty} %ii, {ty} poison, <{lanes} x i32> zeroinitializer\n'
        body += f'%values = shufflevector {ty} {bw}, {ty} poison, <{lanes} x i32> {vec(32, [preferred]*lanes)}\n'
        body += f'%mask = icmp eq {ty} %indices, {vec(width, range(lanes))}\n'
        body += f'%selected = select <{lanes} x i1> %mask, {ty} %values, {ty} {aw}\n'
        if width != 8:
            body += f'%bytes = bitcast {ty} %selected to <16 x i8>\n'
        body += '%out = ' + call('%selected' if width == 8 else '%bytes')
        cases.append((f'insert{width}', 0, size, body))
    cases.append(('table_input', 1, 0, f'%ar = {call("%a")}\n%out = {call("%ar", "%b")}'))
    index = '%count = shufflevector <16 x i8> %b, <16 x i8> poison, <16 x i32> '+vec(32, [12]*16)+'\n'
    index += '%sum = add <16 x i8> %count, '+vec(8, range(16))+'\n%idx = and <16 x i8> %sum, '+vec(8, [15]*16)+'\n'
    cases.append(('table_output', 2, 0, index+f'%t = {call("%a", "%idx")}\n%out = {call("%t")}'))
    for op, kind in [('and', 3), ('or', 4), ('xor', 5)]:
        body = f'%ar = {call("%a")}\n%br = {call("%b")}\n%value = {op} <16 x i8> %ar, %br\n%out = {call("%value")}'
        cases.append((op, kind, 0, body))
    cases.append(('bitselect', 6, 0, f'%ar = {call("%a")}\n%br = {call("%b")}\n%notc = xor <16 x i8> %c, {vec(8, [255]*16)}\n%x = and <16 x i8> %ar, %c\n%y = and <16 x i8> %br, %notc\n%value = or <16 x i8> %x, %y\n%out = {call("%value")}'))
    cases.append(('scalar_select', 7, 0, f'%ar = {call("%a")}\n%br = {call("%b")}\n%cc = extractelement <16 x i8> %c, i32 0\n%condition = icmp ne i8 %cc, 0\n%value = select i1 %condition, <16 x i8> %ar, <16 x i8> %br\n%out = {call("%value")}'))
    cases.append(('guard_shared', 8, 0, f'%ar = {call("%a")}\n%br = {call("%b")}\n%value = xor <16 x i8> %ar, %br\n%r = {call("%value")}\n%out = xor <16 x i8> %value, %r'))
    cases.append(('guard_predicate', 9, 0, f'%ar = {call("%a")}\n%br = {call("%b")}\n%cond = icmp ne <16 x i8> %c, zeroinitializer\n%value = select <16 x i1> %cond, <16 x i8> %ar, <16 x i8> %br\n%out = {call("%value")}'))
    cases.append(('guard_reverse', 10, 0, '%out = '+call('%a')))
    tbx2 = '@llvm.aarch64.neon.tbx2.v16i8'
    for side in (0, 1):
        for swapped in (0, 1):
            for splat in (0, 37, 128, 255):
                lut = vec(8, [0]*12 + [255, 255, 128, 128])
                body = '%hi = lshr <16 x i8> %c, '+vec(8, [4]*16)+'\n%base = '+call(lut, '%hi')+'\n'
                body += '%cv = xor <16 x i8> %c, '+vec(8, [0 if swapped else 15]*16)+'\n'
                body += '%cm = and <16 x i8> %cv, '+vec(8, [159]*16)+'\n'
                a, b = (vec(8, [splat]*16), '%b') if side == 0 else ('%a', vec(8, [splat]*16))
                body += f'%out = call <16 x i8> {tbx2}(<16 x i8> %base, <16 x i8> {a}, <16 x i8> {b}, <16 x i8> %cm)'
                cases.append((f'splat_{side}_{swapped}_{splat}', 11, splat | side << 8 | swapped << 9, body))
    tbl2 = '@llvm.aarch64.neon.tbl2.v16i8'
    for kind in (12, 13, 14):
        for side in (0, 1):
            for swapped in (0, 1):
                body = '%masked = and <16 x i8> %c, '+vec(8, [239]*16)+'\n'
                body += '%selector = or <16 x i8> %masked, '+vec(8, [side*16]*16)+'\n'
                body += '%cv = xor <16 x i8> %selector, '+vec(8, [0 if swapped else 15]*16)+'\n'
                if kind == 14:
                    body += '%hi = lshr <16 x i8> %selector, '+vec(8, [4]*16)+'\n'
                    body += '%base = '+call(vec(8, [0]*12+[255,255,128,128]), '%hi')+'\n'
                    body += '%idx = and <16 x i8> %cv, '+vec(8, [159]*16)+'\n'
                else:
                    body += '%base = xor <16 x i8> %a, %b\n'
                    body += '%idx = xor <16 x i8> %cv, zeroinitializer\n'
                body += f'%out = call <16 x i8> {tbl2 if kind == 12 else tbx2}('
                body += ('' if kind == 12 else '<16 x i8> %base, ')
                body += '<16 x i8> %a, <16 x i8> %b, <16 x i8> %idx)'
                cases.append((f'single_{kind}_{side}_{swapped}', kind, side | swapped << 1, body))
    for kind in (12, 13):
        for mixed in (False, True):
            # Unknown source bits and nonuniform per-lane source selection
            # must retain the two-register table.
            body = '%base = xor <16 x i8> %a, %b\n'
            if mixed:
                body += '%masked = and <16 x i8> %c, '+vec(8, [239]*16)+'\n'
                body += '%idx = or <16 x i8> %masked, '+vec(8, [(i%2)*16 for i in range(16)])+'\n'
            else:
                body += '%idx = xor <16 x i8> %c, zeroinitializer\n'
            body += f'%out = call <16 x i8> {tbl2 if kind == 12 else tbx2}('
            body += ('' if kind == 12 else '<16 x i8> %base, ')
            body += '<16 x i8> %a, <16 x i8> %b, <16 x i8> %idx)'
            cases.append((f'guard_table_{kind}_{int(mixed)}', kind, 4 | int(mixed) << 3, body))
    from spu_byte_cases import CASES
    cases.extend(CASES)
    functions, tests = [], []
    for name, kind, size, body in cases:
        for chain in range(2 if kind >= 52 else 4):
            fname = name + ('', '_chain', '_chain_b', '_chain_c')[chain]
            text = f'define void @{fname}_baseline(ptr %asrc, ptr %bsrc, ptr %csrc, ptr %dst, i64 %n) {{\nentry:\n  br label %loop\nloop:\n  %i = phi i64 [0, %entry], [%next, %loop]\n'
            if chain:
                text += '  %state = phi <16 x i8> [zeroinitializer, %entry], [%out, %loop]\n'
            for k, arg in enumerate('abc', 1):
                loaded = f'%raw_{arg}' if chain == k else '%'+arg
                text += f'  %{arg}p = getelementptr <16 x i8>, ptr %{arg}src, i64 %i\n  {loaded} = load <16 x i8>, ptr %{arg}p, align 1\n'
                if chain == k:
                    text += f'  %{arg} = xor <16 x i8> {loaded}, %state\n'
            text += '  '+body.replace('\n', '\n  ')+'\n'
            text += '  %dp = getelementptr <16 x i8>, ptr %dst, i64 %i\n  store <16 x i8> %out, ptr %dp, align 1\n  %next = add i64 %i, 1\n  %again = icmp ult i64 %next, %n\n  br i1 %again, label %loop, label %end, !llvm.loop !0\nend:\n  ret void\n}\n'
            functions.append(text)
            tests.append((fname, kind, size, chain))
    declarations = '\n'.join('declare <16 x i8> @llvm.'+name+'.v16i8(<16 x i8>'+('' if name == 'ctpop' else ', <16 x i8>')+')' for name in ['ctpop','umin','umax','smin','smax','aarch64.neon.uabd','aarch64.neon.urhadd'])+'\n'
    output.write_text(declarations+f'declare <16 x i8> {tbl}(<16 x i8>, <16 x i8>)\n'+f'declare <16 x i8> {tbl2}(<16 x i8>, <16 x i8>, <16 x i8>)\n'+f'declare <16 x i8> {tbx2}(<16 x i8>, <16 x i8>, <16 x i8>, <16 x i8>)\n'+'\n'.join(functions)+'\n!0 = distinct !{!0, !1}\n!1 = !{!"llvm.loop.unroll.disable"}\n')
    header = 'using fn=void(*)(const void*,const void*,const void*,void*,unsigned long long);\n'
    for name, *_ in tests:
        for variant in ('baseline', 'candidate'):
            header += f'extern "C" void {name}_{variant}(const void*,const void*,const void*,void*,unsigned long long);\n'
    header += 'struct test { const char* name; unsigned kind, size, chain; fn functions[2]; };\nconst test tests[]={\n'
    for name, kind, size, chain in tests:
        header += f'{{"{name}",{kind},{size},{chain},{{{name}_baseline,{name}_candidate}}}},\n'
    output.with_suffix('.h').write_text(header+'};\n')
    print(f'Emitted {len(tests)} baseline shapes, including no-transform guards, each with four dependency layouts.')

if __name__ == '__main__':
    generate(Path(sys.argv[1]))
