#include <asmjit/a64.h>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
using namespace asmjit;
using u64 = uint64_t;
struct context { u64 cia = 0; unsigned calls = 0; };
context* expected;
context* escaped;
void far_jump(context* thread, u64, void* op)
{
    assert(thread == expected);
    assert(op == reinterpret_cast<void*>(0x10003000));
    thread->cia = 0x123456;
    ++thread->calls;
}
namespace vm { inline const void* g_base_addr = reinterpret_cast<void*>(0x10000000); }
const auto ppu_far_jump = &far_jump;
void (*ppu_escape)();
template<class F> void (*build(JitRuntime& runtime, F emit))()
{
    CodeHolder code;
    assert(code.init(runtime.environment()) == kErrorOk);
    a64::Assembler c(&code);
    emit(c);
    void (*fn)() = nullptr;
    assert(runtime.add(&fn, &code) == kErrorOk);
    return fn;
}
int main()
{
    JitRuntime runtime;
    ppu_escape = build(runtime, [](a64::Assembler& c)
    {
        c.mov(a64::x9, Imm(reinterpret_cast<u64>(&escaped)));
        c.str(a64::x0, arm::Mem(a64::x9));
        // Independent host-side escape restores the gateway frame, not the
        // return address of far_jump's BLR inside the production stub.
        c.ldp(a64::x20, a64::x30, arm::Mem(a64::sp));
        c.add(a64::sp, a64::sp, Imm(16));
        c.ret(a64::x30);
    });
    const auto stub = build(runtime, [](a64::Assembler& c)
    {
        const u64 pc = 0x3000;
        const std::array args{a64::x0, a64::x1, a64::x2, a64::x3};
#include "production.inc"
    });
    const auto gateway = build(runtime, [&](a64::Assembler& c)
    {
        c.sub(a64::sp, a64::sp, Imm(16));
        c.stp(a64::x20, a64::x30, arm::Mem(a64::sp));
        c.mov(a64::x20, a64::x0);
        c.mov(a64::x0, Imm(0x475)); // Poison the former ABI argument.
        c.mov(a64::x13, Imm(reinterpret_cast<u64>(stub)));
        c.br(a64::x13);
    });
    for (unsigned i = 0; i < 10'000; ++i)
    {
        context thread;
        expected = &thread; escaped = nullptr;
        reinterpret_cast<void(*)(context*)>(gateway)(&thread);
        assert(escaped == &thread && thread.cia == 0x123456 && thread.calls == 1);
    }
    std::puts("ARM64 far jump: 10,000 generated GHC entries preserve the thread, update PC and escape to the host.");
}
