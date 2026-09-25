"""Lower the actual conversion's IRBuilder expressions to a host LLVM fixture.

This deliberately accepts only the small, explicit IRBuilder subset used here.
Unknown expressions fail rather than silently substituting a reference algorithm.
The scalar reference in SPUXFloatConversionTests.cpp is independent of this lowering.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
SOURCE = 'rpcs3/Emu/Cell/SPULLVMRecompiler.cpp'

# Original non-AVX512 production IRBuilder expressions (371e72577).
BASELINE = '''
const auto x = m_ir->CreateZExt(val, get_type<u64[4]>());
const auto s = m_ir->CreateShl(m_ir->CreateAnd(x, 0x80000000), 32);
const auto a = m_ir->CreateAnd(x, 0x7fffffff);
const auto m = m_ir->CreateShl(m_ir->CreateAdd(a, splat<u64[4]>(0x1c0000000).eval(m_ir)), 29);
const auto r = m_ir->CreateSelect(m_ir->CreateICmpSGT(a, splat<u64[4]>(0x7fffff).eval(m_ir)), m, splat<u64[4]>(0).eval(m_ir));
const auto f = m_ir->CreateOr(s, r);
return uint64_as_double(f);
'''


def arguments(text):
    result, depth, start = [], 0, 0
    for i, c in enumerate(text):
        if c in '({': depth += 1
        if c in ')}': depth -= 1
        if c == ',' and depth == 0:
            result.append(text[start:i].strip())
            start = i + 1
    return result + [text[start:].strip()]


class Emitter:
    def __init__(self):
        self.lines = []
        self.values = {'val': ('<4 x i32>', '%input')}

    def emit(self, typ, instruction):
        name = f'%v{len(self.lines)}'
        self.lines.append(f'  {name} = {instruction}')
        return typ, name

    def expr(self, text):
        text = text.strip()
        if text in self.values: return self.values[text]
        if re.fullmatch(r'0x[0-9a-f]+|[0-9]+', text): return 'literal', int(text, 0)
        if match := re.fullmatch(r'splat<u(32|64)\[4\]>\((.*?)\).eval\(m_ir\)', text):
            bits, value = match.groups()
            return f'<4 x i{bits}>', '<' + ', '.join([f'i{bits} {int(value, 0)}'] * 4) + '>'
        if match := re.fullmatch(r'(?:bitcast<f64\[4\]>|uint64_as_double)\((.*)\)', text):
            typ, value = self.expr(match[1])
            return self.emit('<4 x double>', f'bitcast {typ} {value} to <4 x double>')
        match = re.fullmatch(r'm_ir->Create(\w+)\((.*)\)', text)
        if not match: raise ValueError(f'Unsupported production expression: {text}')
        op, args = match[1], arguments(match[2])
        left_type, left = self.expr(args[0])
        if op == 'ZExt':
            assert args[1] == 'get_type<u64[4]>()'
            return self.emit('<4 x i64>', f'zext {left_type} {left} to <4 x i64>')
        if op == 'Select':
            rt, right = self.expr(args[1]); ft, false = self.expr(args[2]); assert rt == ft
            return self.emit(rt, f'select {left_type} {left}, {rt} {right}, {ft} {false}')
        rt, right = self.expr(args[1])
        if op == 'ShuffleVector':
            indices = [int(i) for i in args[2].strip('{}').split(',')]
            assert left_type == rt == '<4 x i32>' and len(indices) == 8
            mask = '<' + ', '.join(f'i32 {i}' for i in indices) + '>'
            return self.emit('<8 x i32>', f'shufflevector {left_type} {left}, {rt} {right}, <8 x i32> {mask}')
        if rt == 'literal':
            scalar = left_type.removeprefix('<4 x ').removesuffix('>')
            right = '<' + ', '.join([f'{scalar} {right}'] * 4) + '>'
            rt = left_type
        assert rt == left_type
        if op in ('ICmpUGT', 'ICmpSGT'):
            return self.emit('<4 x i1>', f'icmp {op[4:].lower()} {left_type} {left}, {right}')
        names = {'And': 'and', 'Or': 'or', 'Add': 'add', 'Shl': 'shl', 'LShr': 'lshr'}
        return self.emit(left_type, f'{names[op]} {left_type} {left}, {right}')


def fixture(body, name):
    emitter = Emitter()
    # Comments carry no IR; only explicitly recognized declarations and return remain.
    body = re.sub(r'//[^\n]*', '', body)
    output = None
    for statement in body.split(';'):
        statement = statement.strip()
        if not statement: continue
        if match := re.fullmatch(r'const auto (\w+) = (.*)', statement, re.S):
            emitter.values[match[1]] = emitter.expr(match[2])
        elif statement.startswith('return '):
            output = emitter.expr(statement[7:])
        else: raise ValueError(f'Unsupported production statement: {statement}')
    assert output and output[0] == '<4 x double>'
    # Four-lane blocks, no FP arithmetic, no fast-math, no alias assumptions.
    return f'''define void @{name}(ptr %src, ptr %dst, i64 %count) {{
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %next, %loop ]
  %sp = getelementptr i32, ptr %src, i64 %i
  %dp = getelementptr i64, ptr %dst, i64 %i
  %input = load <4 x i32>, ptr %sp, align 4
{chr(10).join(emitter.lines)}
  store <4 x double> {output[1]}, ptr %dp, align 8
  %next = add i64 %i, 4
  %again = icmp ult i64 %next, %count
  br i1 %again, label %loop, label %end
end:
  ret void
}}
'''


def main(output):
    current = (ROOT / SOURCE).read_text()
    start = '\tllvm::Value* xfloat_to_double(llvm::Value* val)'
    current = current.split(start, 1)[1].split('#ifdef ARCH_ARM64', 1)[1].split('#else', 1)[0]
    Path(output).write_text(fixture(BASELINE, 'baseline') + '\n' + fixture(current, 'candidate'))


if __name__ == '__main__': main(sys.argv[1])
