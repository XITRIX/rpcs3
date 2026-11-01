// Architecture proof only: deliberately excluded from the production core.
// Exercise native PPU lowering and its ARM64 GHC ABI from an immutable image.
#include "ios/AOTCompiler.h"
#include "ios/AOTDispatch.h"
#include "ios/AOTNativeGateway.h"
#include "ios/AOTNativeRuntime.h"
#include "ios/AOTNativeModule.h"
#include "ios/AOTNativeImports.h"
#include "ios/AOTRecompiler.h"
#include "Emu/Cell/PPUTranslator.h"
#include "Emu/Cell/PPUThread.h"
#include "Emu/Cell/PPUInterpreter.h"
#include "Emu/Cell/SPUThread.h"
#include "Emu/Cell/SPUInterpreter.h"
#include "util/simd.hpp"
#include "Emu/Cell/SPURecompiler.h"
#include "Emu/Cell/SPUOpcodes.h"
#include "Emu/system_config.h"
#include "Emu/Memory/vm.h"
#include "Emu/Cell/lv2/sys_sync.h"
#include "Utilities/JITIOS.h"
#include <llvm/IR/Verifier.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Linker/Linker.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <random>
#include <cfenv>

// Prebuilt C -> GHC bridge. The native convention treats SP as a shared scratch
// base; it must not return through an ordinary C compiler-generated frame.
// Preserve Darwin callee-saved GPR/SIMD registers. x18 is never touched.
extern void ppu_register_range(u32, u32);
extern void ppu_register_function_at(u32, u32, ppu_intrp_func_t);
extern const std::unordered_map<std::string, u64>& ppu_llvm_runtime_symbols();
extern const std::unordered_map<std::string, u64>& ppu_native_runtime_symbols();



// Test-only caller poisons every Darwin callee-saved register around the
// prebuilt gateway. x18 is observed but never written.
extern "C" bool rpcs3_aot_test_registers(void*, const u64*, u64*);
asm(R"(
.text
.globl _rpcs3_aot_test_registers
.p2align 2
_rpcs3_aot_test_registers:
sub sp, sp, #176
stp x19, x20, [sp, #0]
stp x21, x22, [sp, #16]
stp x23, x24, [sp, #32]
stp x25, x26, [sp, #48]
stp x27, x28, [sp, #64]
stp x29, x30, [sp, #80]
stp d8, d9, [sp, #96]
stp d10, d11, [sp, #112]
stp d12, d13, [sp, #128]
stp d14, d15, [sp, #144]
str x18, [sp, #160]
mov x19, #4115
mov x20, #4116
mov x21, #4117
mov x22, #4118
mov x23, #4119
mov x24, #4120
mov x25, #4121
mov x26, #4122
mov x27, #4123
mov x28, #4124
mov x29, #4125
mov x9, #8200
fmov d8, x9
mov x9, #8201
fmov d9, x9
mov x9, #8202
fmov d10, x9
mov x9, #8203
fmov d11, x9
mov x9, #8204
fmov d12, x9
mov x9, #8205
fmov d13, x9
mov x9, #8206
fmov d14, x9
mov x9, #8207
fmov d15, x9
bl _rpcs3_aot_native_enter
mov w0, #1
ldr x9, [sp, #160]
cmp x18, x9
b.ne Lnative_register_bad
mov x9, #4115
cmp x19, x9
b.ne Lnative_register_bad
mov x9, #4116
cmp x20, x9
b.ne Lnative_register_bad
mov x9, #4117
cmp x21, x9
b.ne Lnative_register_bad
mov x9, #4118
cmp x22, x9
b.ne Lnative_register_bad
mov x9, #4119
cmp x23, x9
b.ne Lnative_register_bad
mov x9, #4120
cmp x24, x9
b.ne Lnative_register_bad
mov x9, #4121
cmp x25, x9
b.ne Lnative_register_bad
mov x9, #4122
cmp x26, x9
b.ne Lnative_register_bad
mov x9, #4123
cmp x27, x9
b.ne Lnative_register_bad
mov x9, #4124
cmp x28, x9
b.ne Lnative_register_bad
mov x9, #4125
cmp x29, x9
b.ne Lnative_register_bad
fmov x10, d8
mov x9, #8200
cmp x10, x9
b.ne Lnative_register_bad
fmov x10, d9
mov x9, #8201
cmp x10, x9
b.ne Lnative_register_bad
fmov x10, d10
mov x9, #8202
cmp x10, x9
b.ne Lnative_register_bad
fmov x10, d11
mov x9, #8203
cmp x10, x9
b.ne Lnative_register_bad
fmov x10, d12
mov x9, #8204
cmp x10, x9
b.ne Lnative_register_bad
fmov x10, d13
mov x9, #8205
cmp x10, x9
b.ne Lnative_register_bad
fmov x10, d14
mov x9, #8206
cmp x10, x9
b.ne Lnative_register_bad
fmov x10, d15
mov x9, #8207
cmp x10, x9
b.ne Lnative_register_bad
b Lnative_register_done
Lnative_register_bad:
mov w0, #0
Lnative_register_done:
ldp x19, x20, [sp, #0]
ldp x21, x22, [sp, #16]
ldp x23, x24, [sp, #32]
ldp x25, x26, [sp, #48]
ldp x27, x28, [sp, #64]
ldp x29, x30, [sp, #80]
ldp d8, d9, [sp, #96]
ldp d10, d11, [sp, #112]
ldp d12, d13, [sp, #128]
ldp d14, d15, [sp, #144]
add sp, sp, #176
ret
)");

namespace
{
thread_local int helper_mode = 0, helper_destructors = 0;
thread_local void* nested_entry = nullptr;
thread_local ppu_thread* nested_cpu = nullptr;
thread_local std::array<u64, 7> nested_arguments{};
void native_spu_escape_fixture(spu_thread* cpu)
{
 if (rpcs3::ios::jit::is_jitless()) spu_runtime::g_escape(cpu);
 // The comparison uses the same prebuilt gateway for both relocation modes;
 // the ordinary JIT gateway owns a different saved-frame representation.
 if (!rpcs3::ios::aot::escape_native_if_active(cpu))
  throw std::runtime_error("SPU escape without native invocation");
}
bool native_spu_check_fixture(spu_thread* cpu) { return cpu->check_state_with_interrupts(); }
u64 native_helper_fixture()
{
 struct cleanup { ~cleanup() { ++helper_destructors; } } guard;
 if (helper_mode == 1) throw std::runtime_error("native helper exception proof");
 if (helper_mode == 2)
  rpcs3::ios::aot::invoke_native(nested_entry, nested_arguments, nested_cpu, nested_cpu->hv_ctx);
 return 71;
}
}

void test_aot_native_abi_load(const std::string&, const std::string&, bool);
namespace rpcs3::ios::aot { void test_native_ppu_loader(const ppu_module<lv2_obj>&, ppu_thread&, const std::string&); }

static std::vector<be_t<u32>> native_ppu_fixture()
{
 std::vector<be_t<u32>> words(0x10000 / 4);
 words[0] = ppu_instructions::ADDI(4, 4, 3);
 words[1] = ppu_instructions::B(4, false, false);
 words[2] = ppu_instructions::ADDI(5, 5, 7);
 words[3] = ppu_instructions::BC(16, 0, -12, false, false);
 words[4] = ppu_instructions::BCLR(20, 0, 0, false);
 ppu_opcode_t mftb{31u << 26 | 0x173u << 1}; mftb.rd = 7; mftb.spr = (0x10cu >> 5) | ((0x10cu & 31) << 5);
 words[8] = mftb.opcode; words[9] = ppu_instructions::ADDI(8, 7, 5);
 words[10] = ppu_instructions::BCLR(20, 0, 0, false);
 words[12] = ppu_instructions::B(16, false, false);
 words[20] = (3u << 26) | (31u << 21); // Unconditional TWI.
 words[21] = ppu_instructions::BCLR(20, 0, 0, false);
 words[24] = ppu_instructions::LD(4, 3, 32);
 words[25] = ppu_instructions::STW(4, 3, 80);
 words[26] = ppu_instructions::LWZ(5, 3, 80);
 words[27] = ppu_instructions::BCLR(20, 0, 0, false);
 words[28] = 0xfc85302au; // FADD f4, f5, f6.
 words[29] = ppu_instructions::BCLR(20, 0, 0, false);
 return words;
}

void test_aot_native_abi(const std::string& directory)
{
 using namespace llvm;
 namespace aot = rpcs3::ios::aot;
 const auto require = [](bool ok, const std::string& why) { if (!ok) throw std::runtime_error(why); };
 // Native module partitioning must retain function boundaries, ownership,
 // relocation metadata through the parent, and hook exclusions.
 ppu_module<lv2_obj> partition_source;
 partition_source.segs.push_back({0x10000, 0x40000, 1, 5, 0, nullptr});
 partition_source.is_relocatable = true;
 partition_source.attr += ppu_attr::has_mfvscr;
 for (u32 i = 0; i < 1005; ++i)
 { ppu_function function; function.addr = 0x10000 + i * 128; function.size = 128; partition_source.funcs.push_back(function); }
 usz position = 0;
 ppu_module<lv2_obj> first_part, second_part;
 first_part.copy_next_compilation_part(partition_source, position, [](const ppu_function& function) { return function.addr == 0x10000; });
 require(position == 1001 && first_part.parent == &partition_source && first_part.excluded_funcs == std::vector<u32>{0x10000} &&
     first_part.local_bounds == std::pair<u32, u32>{0x10080, 0x10000 + 1001 * 128} && first_part.is_relocatable &&
     first_part.attr == partition_source.attr, "native PPU partition lost boundaries/metadata/exclusions");
 second_part.copy_next_compilation_part(partition_source, position, [](const ppu_function&) { return false; });
 require(position == 1005 && second_part.get_funcs().size() == 4 && second_part.local_bounds.first == first_part.local_bounds.second,
     "native PPU partition skipped or split a function");
 LLVMContext context;
 Module module("native-abi-proof", context);
 auto options = aot::native_object_options();
#ifdef RPCS3_IOS_AOT_SIMULATOR
 options.platform = aot::object_platform::ios_simulator;
#else
 options.platform = aot::object_platform::host_macos;
#endif
 std::string error;
 require(aot::configure_object_module(module, options, error), error);
 // Two native functions, many alternating tail transfers, then a native exit.
 // No AOT fragment context, instruction budget, descriptor lookup or byte scan.
 auto words = native_ppu_fixture();
 ppu_module<lv2_obj> info;
 info.is_relocatable = true;
 info.local_bounds = {0x10000, 0x10080};
 info.segs.push_back({0x10000, 128, 1, 5, 128, words.data()});
 info.addr_to_seg_index.emplace(0x10000, 0);
 IRBuilder<> ir(context);
 auto ptr = ir.getPtrTy();
 auto type = FunctionType::get(ir.getVoidTy(), {ptr, ptr, ir.getInt64Ty(), ptr,
     ir.getInt64Ty(), ir.getInt64Ty(), ir.getInt64Ty()}, false);
 for (auto [offset, size] : {std::pair{0u, 8u}, {8u, 8u}, {16u, 4u}, {32u, 12u}, {48u, 4u}, {80u, 8u}, {96u, 16u}, {112u, 8u}})
 {
  ppu_function function;
  function.addr = 0x10000 + offset; function.size = size;
  info.funcs.push_back(function);
 }
 // The same whole-module scan must precede either compiler's partitions.
 require(!info.prepare_compilation_attributes(true), "MFVSCR falsely detected");
 const auto saved_word = words[0];
 words[0] = (4u << 26) | 0x604u; // MFVSCR v0.
 require(!info.prepare_compilation_attributes(false) && !(info.attr & ppu_attr::has_mfvscr),
     "disabled SAT scan changed native attributes");
 require(info.prepare_compilation_attributes(true) && (info.attr & ppu_attr::has_mfvscr),
     "whole-module native SAT metadata missing");
 info.attr -= ppu_attr::has_mfvscr;
 info.funcs.front().blocks.emplace(0x10000, 8);
 require(info.prepare_compilation_attributes(true), "explicit-block native SAT metadata missing");
 info.funcs.front().blocks.clear();
 words[0] = saved_word;
 info.attr -= ppu_attr::has_mfvscr;
 info.prepare_compilation_attributes(g_cfg.core.ppu_set_sat_bit.get());
 std::vector<std::pair<u32, Function*>> native_entries;
 ppu_module<lv2_obj> native_part;
 usz native_position = 0;
 native_part.copy_next_compilation_part(info, native_position, [](const ppu_function&) { return false; });
 require(aot::translate_ppu_native(module, native_part, options.cpu, native_entries), "native PPU module translation failed");
 require(native_entries.size() == info.funcs.size(), "native PPU module entries lost loader metadata");
 auto external_target = module.getFunction("__0x40");
 require(external_target && !external_target->isDeclaration(), "native external branch stub missing");
 std::vector<be_t<u32>> local_store(SPU_LS_SIZE / 4);
 local_store[0x100 / 4] = spu_instructions::AI(4, 4, 3);
 local_store[0x104 / 4] = spu_instructions::AI(5, 5, -1);
 local_store[0x108 / 4] = spu_instructions::BRNZ(5, -8);
 local_store[0x10c / 4] = (0x35000000u | (6u << 7));
 const auto saved_block_size = g_cfg.core.spu_block_size.get();
 g_cfg.core.spu_block_size.set(spu_block_size_type::mega);
 auto spu_module = std::make_unique<Module>("native-spu-proof", context);
 require(aot::configure_object_module(*spu_module, options, error), error);
 spu_program native_spu_identity;
 auto spu_entry = aot::translate_spu_native(*spu_module, local_store.data(), 0x100, options.cpu, "native_spu_proof", &native_spu_identity);
 require(native_spu_identity.entry_point == 0x100 && !native_spu_identity.data.empty(), "native SPU identity missing");
 g_cfg.core.spu_block_size.set(saved_block_size);
 require(spu_entry, "native SPU translation failed");
 const auto spu_name = spu_entry->getName().str();
 require(!Linker::linkModules(module, std::move(spu_module)), "native proof IR link failed");
 spu_entry = module.getFunction(spu_name);
 require(spu_entry, "native SPU entry lost during IR link");
 // Compile caller and callee from independent snapshots so a return cannot
 // accidentally succeed through a local direct edge or local function table.
 std::vector<aot::native_return_definition> native_returns;
 auto compile_spu = [&](std::string name, u32 pc, bool caller)
 {
  std::vector<be_t<u32>> snapshot(SPU_LS_SIZE / 4);
  if (name == "native_spu_float")
  {
   spu_opcode_t fa{0x2c4u << 21}; fa.rt = 4; fa.ra = 5; fa.rb = 6;
   snapshot[0x180 / 4] = fa.opcode;
   snapshot[0x184 / 4] = 0x35000000u | (127u << 7);
  }
  else if (name == "native_spu_memory")
  {
   snapshot[0x1c0 / 4] = spu_instructions::LQD(4, 3, 32);
   snapshot[0x1c4 / 4] = spu_instructions::STQD(4, 3, 64);
   snapshot[0x1c8 / 4] = 0x35000000u | (127u << 7);
  }
  else if (name == "native_spu_channel")
  {
   spu_opcode_t rd{0xdu << 21}; rd.ra = SPU_RdInMbox; rd.rt = 23;
   snapshot[0x140 / 4] = rd.opcode;
   snapshot[0x144 / 4] = 0x35000000u | (6u << 7);
  }
  else if (caller)
  {
   spu_opcode_t branch{0x66u << 23}; branch.rt = 0; branch.si16 = 8;
   snapshot[0x100 / 4] = branch.opcode;
   snapshot[0x104 / 4] = spu_instructions::AI(20, 20, 3);
   snapshot[0x108 / 4] = spu_instructions::AI(21, 21, -1);
   snapshot[0x10c / 4] = spu_instructions::BRNZ(21, -12);
   snapshot[0x110 / 4] = 0x35000000u | (6u << 7);
  }
  else
  {
   snapshot[0x120 / 4] = spu_instructions::AI(22, 22, 1);
   snapshot[0x124 / 4] = 0x35000000u;
  }
  auto part = std::make_unique<Module>(name, context);
  require(aot::configure_object_module(*part, options, error), error);
  g_cfg.core.spu_block_size.set(spu_block_size_type::mega);
  auto function = aot::translate_spu_native(*part, snapshot.data(), pc, options.cpu, name);
  g_cfg.core.spu_block_size.set(saved_block_size);
  require(function, "native call/return translation failed");
  const auto function_name = function->getName().str();
  const auto field = [&](const char* suffix, Type* value_type)
  {
   const auto symbol = name + ".aot_spu_return_" + suffix;
   if (auto global = part->getNamedGlobal(symbol)) return global;
   return new GlobalVariable(*part, value_type, false, GlobalValue::InternalLinkage,
       Constant::getNullValue(value_type), symbol);
  };
  auto chunks = part->getNamedGlobal(name + ".aot_spu_return_chunks");
  require(chunks, "native return chunk metadata missing");
  const auto chunk_count = cast<ArrayType>(chunks->getValueType())->getNumElements();
  require(!caller || chunk_count > 0, "caller has no native return chunks");
  auto size = new GlobalVariable(*part, ir.getInt32Ty(), true, GlobalValue::InternalLinkage,
      ir.getInt32(chunk_count), name + ".return_size");
  const auto metadata_name = name + ".return_metadata";
  new GlobalVariable(*part, ArrayType::get(ptr, 5), true, GlobalValue::ExternalLinkage,
      ConstantArray::get(ArrayType::get(ptr, 5), {field("base", ir.getInt32Ty()), field("count", ir.getInt32Ty()),
      field("targets", ptr), chunks, size}), metadata_name);
  require(!Linker::linkModules(module, std::move(part)), "native call/return IR link failed");
  native_returns.push_back({module.getNamedGlobal(name + ".aot_spu_return_base"), module.getNamedGlobal(name + ".aot_spu_return_count"),
      module.getNamedGlobal(name + ".aot_spu_return_targets"), module.getNamedGlobal(name + ".aot_spu_return_chunks"), static_cast<u32>(chunk_count)});
  return std::pair{module.getFunction(function_name), module.getNamedGlobal(metadata_name)};
 };
 auto caller = compile_spu("native_spu_caller", 0x100, true);
 auto callee = compile_spu("native_spu_callee", 0x120, false);
 auto channel = compile_spu("native_spu_channel", 0x140, false);
 auto floating = compile_spu("native_spu_float", 0x180, false);
 auto memory = compile_spu("native_spu_memory", 0x1c0, false);
 std::vector<Function*> imported_functions;
 for (auto& function : module)
  if (function.isDeclaration() && !function.isIntrinsic() && !function.getName().starts_with("__") && aot::resolve_native_import(function.getName().str()))
   imported_functions.push_back(&function);
 require(!imported_functions.empty(), "native SPU helper imports were not emitted");
 const auto imported = aot::materialize_runtime_imports(module, imported_functions);
 std::vector<aot::native_entry_definition> described_entries;
 for (const auto& [address, target] : native_entries)
  described_entries.push_back({1, address, static_cast<u32>(described_entries.size()), target});
 for (const auto& [address, target] : std::array<std::pair<u32, Function*>, 6>{{{0x100, spu_entry}, {0x100, caller.first},
     {0x120, callee.first}, {0x140, channel.first}, {0x180, floating.first}, {0x1c0, memory.first}}})
  described_entries.push_back({2, address, static_cast<u32>(described_entries.size()), target});
 aot::emit_native_descriptor(module, described_entries, imported, native_returns);
 auto import_count = new GlobalVariable(module, ir.getInt32Ty(), true, GlobalValue::InternalLinkage,
     ir.getInt32(imported.count), "native_import_count");
 auto dispatch_count = new GlobalVariable(module, ir.getInt64Ty(), false, GlobalValue::InternalLinkage, ir.getInt64(0), "native_dispatch_count");
 auto bindings_type = ArrayType::get(ptr, 7);
 auto binding = new GlobalVariable(module, bindings_type, false, GlobalValue::ExternalLinkage,
     ConstantAggregateZero::get(bindings_type), "rpcs3_aot_register_runtime_v2");
 // Resolve only the native state-check and terminal exits. These are proof
 // adapters, not replacements for production CPU-work/exception processing.
 for (auto& f : module)
 {
  if (!f.isDeclaration() || f.isIntrinsic()) continue;
  require(f.getName() == "__trap" || f.getName() == "__get_tb" || f.getName() == "__check" || f.getName() == "__0x14" || f.getName() == "__0x54" || f.getName() == "spu_escape" || f.getName() == "spu_exec_check_state" || f.getName() == "spu_dispatcher" || f.getName() == "spu_dispatch", "unexpected native import: " + f.getName().str());
  ir.SetInsertPoint(BasicBlock::Create(context, "exit", &f));
  if (f.getName() == "__get_tb")
  {
   auto target = ir.CreateLoad(ptr, ir.CreateConstGEP1_64(ptr, binding, 2));
   ir.CreateRet(ir.CreateCall(f.getFunctionType(), target, {}));
   continue;
  }
  if (f.getName() == "__check")
  {
   auto target = ir.CreateLoad(ptr, ir.CreateConstGEP1_64(ptr, binding, 5));
   ir.CreateCall(f.getFunctionType(), target, {f.getArg(0), f.getArg(1)});
   ir.CreateRetVoid();
   continue;
  }
  if (f.getName() == "__trap")
  {
   auto target = ir.CreateLoad(ptr, ir.CreateConstGEP1_64(ptr, binding, 6));
   ir.CreateCall(f.getFunctionType(), target, {f.getArg(0), f.getArg(1)});
   ir.CreateRetVoid();
   continue;
  }
  if (f.getName() == "spu_exec_check_state")
  {
   auto target = ir.CreateLoad(ptr, ir.CreateConstGEP1_64(ptr, binding, 3));
   ir.CreateRet(ir.CreateCall(f.getFunctionType(), target, {f.getArg(0)}));
   continue;
  }
  if (f.getName() == "spu_escape")
  {
   auto restore = ir.CreateLoad(ptr, ir.CreateConstGEP1_64(ptr, binding, 1));
   ir.CreateCall(f.getFunctionType(), restore, {f.getArg(0)});
   ir.CreateUnreachable();
   continue;
  }
  auto target = ir.CreateLoad(ptr, binding);
  auto call = ir.CreateCall(type, target, {ConstantPointerNull::get(ptr), ConstantPointerNull::get(ptr), ir.getInt64(0),
      ConstantPointerNull::get(ptr), ir.getInt64(0), ir.getInt64(0), ir.getInt64(0)});
  call->setCallingConv(CallingConv::GHC); call->setTailCall();
  ir.CreateRetVoid();
 }
 new GlobalVariable(module, ArrayType::get(ptr, 18), true, GlobalValue::ExternalLinkage,
     ConstantArray::get(ArrayType::get(ptr, 18), {module.getFunction("__0x0"), spu_entry, module.getFunction("__0x20"), module.getFunction("__0x30"), caller.first, callee.first, caller.second, callee.second, module.getFunction("__0x50"), imported.names, imported.table, channel.first, import_count, dispatch_count, module.getFunction("__0x60"), module.getFunction("__0x70"), floating.first, memory.first}), "rpcs3_aot_module_v2");
 require(!verifyModule(module, &errs()), "invalid native ABI IR");
 std::error_code ec;
 raw_fd_ostream ir_file(directory + "/native-abi.ll", ec);
 require(!ec, ec.message()); module.print(ir_file, nullptr); ir_file.close();
 require(aot::validate_native_ir(module, error), error);
 require(!aot::validate_ir(module, error), "fragment validator accepted native ABI output");
 auto captured_address = new GlobalVariable(module, ptr, true, GlobalValue::InternalLinkage,
     ConstantExpr::getIntToPtr(ir.getInt64(0x100000000ull), ptr), "invalid_process_address");
 require(!aot::validate_native_ir(module, error), "native validator accepted a captured process address");
 captured_address->eraseFromParent();
 auto unknown_import = Function::Create(FunctionType::get(ir.getVoidTy(), false), GlobalValue::ExternalLinkage,
     "unknown_native_import", module);
 require(!aot::validate_native_ir(module, error), "native validator accepted an unbound import");
 unknown_import->eraseFromParent();
 auto invalid_assembly = Function::Create(FunctionType::get(ir.getVoidTy(), false), GlobalValue::InternalLinkage,
     "invalid_native_assembly", module);
 invalid_assembly->setCallingConv(CallingConv::GHC);
 ir.SetInsertPoint(BasicBlock::Create(context, "entry", invalid_assembly));
 ir.CreateCall(InlineAsm::get(invalid_assembly->getFunctionType(), "mov x18, x0", "~{x18}", true));
 ir.CreateRetVoid();
 require(!aot::validate_native_ir(module, error), "native validator accepted reserved-register assembly");
 invalid_assembly->eraseFromParent();
 auto jit_module = CloneModule(module);
 std::vector<char> bytes;
 require(aot::emit_native_object(module, options, bytes, error), error);
 std::cout << "Native ABI emission: shared writer and address/import/assembly rejection passed\n";
 const auto object = directory + "/native-abi.o", library = directory + "/native-abi.dylib";
 std::ofstream(object, std::ios::binary).write(bytes.data(), bytes.size());
 const std::string objects[] = {object};
 const std::string native_exports[] = {"_rpcs3_aot_native_module", "_rpcs3_aot_module_v2", "_rpcs3_aot_register_runtime_v2"};
 require(aot::link_module(objects, library, "@rpath/rpcs3-aot-native-abi.dylib", options.platform, error, native_exports), error);
 require(aot::link_module(objects, directory + "/native-peer.dylib", "@rpath/rpcs3-aot-native-peer.dylib", options.platform, error, native_exports), error);
 // RPCS3's native ARM64 JIT consumes ELF (Android triple reserves x18).
 // Compare identical pre-codegen IR under its actual backend, not a different
 // Mach-O-capable JIT. Both object files are retained for instruction audit.
 const llvm::Triple jit_triple(jit_compiler::triple1());
 const auto jit_target = TargetRegistry::lookupTarget(jit_triple, error);
 require(jit_target, error);
 std::unique_ptr<TargetMachine> jit_machine(jit_target->createTargetMachine(jit_triple, options.cpu,
     options.features, TargetOptions{}, Reloc::PIC_, CodeModel::Small, CodeGenOptLevel::Aggressive));
 require(bool(jit_machine), "native JIT target unavailable");
 jit_module->setTargetTriple(jit_triple); jit_module->setDataLayout(jit_machine->createDataLayout());
 SmallVector<char, 0> jit_bytes; raw_svector_ostream jit_stream(jit_bytes); legacy::PassManager jit_passes;
 require(!jit_machine->addPassesToEmitFile(jit_passes, jit_stream, nullptr, CodeGenFileType::ObjectFile), "native JIT object unavailable");
 jit_passes.run(*jit_module);
 std::ofstream(directory + "/native-abi-jit.o", std::ios::binary).write(jit_bytes.data(), jit_bytes.size());
 aot::instruction program{2, spu_instructions::AI(4, 4, 3),
     {spu_instructions::AI(4, 4, 3), spu_instructions::AI(5, 5, -1), spu_instructions::BRNZ(5, -8), 0x35000000u | (6u << 7)},
     0x100, 0, {{0, 4}}};
 options.native_spu = true;
 aot::object_statistics pipeline_stats;
 require(aot::emit_instructions(std::span(&program, 1), options, bytes, error, &pipeline_stats), error);
 require(pipeline_stats.spu == 1, "production pipeline declined native SPU program");
 const auto pipeline_object = directory + "/native-pipeline.o";
 std::ofstream(pipeline_object, std::ios::binary).write(bytes.data(), bytes.size());
 const std::string pipeline_objects[] = {pipeline_object};
 require(aot::link_module(pipeline_objects, directory + "/native-pipeline.dylib", "@rpath/rpcs3-aot-native-pipeline.dylib",
     options.platform, error, native_exports), error);
 // Feed the real native partition API, retaining whole functions/TOC/relocations.
 require(!aot::emit_ppu_native(native_part, options, bytes, error, [] { return true; }) && bytes.empty(),
     "cancelled native PPU emission published an object");
 require(aot::emit_ppu_native(native_part, options, bytes, error), error);
 const auto ppu_object = directory + "/native-ppu-pipeline.o";
 std::ofstream(ppu_object, std::ios::binary).write(bytes.data(), bytes.size());
 const std::string ppu_objects[] = {ppu_object};
 const std::string ppu_exports[] = {"_rpcs3_aot_native_module"};
 require(aot::link_module(ppu_objects, directory + "/native-ppu-pipeline.dylib", "@rpath/rpcs3-aot-native-ppu-pipeline.dylib",
     options.platform, error, ppu_exports), error);
 test_aot_native_abi_load(directory, directory, false);
}

void test_aot_native_abi_load(const std::string& directory, const std::string& source, bool use_jit)
{
 const auto require = [](bool ok, const std::string& why) { if (!ok) throw std::runtime_error(why); };
 std::unique_ptr<jit_compiler> jit, peer_jit;
 void** peer_entry = nullptr; void** peer_runtime = nullptr;
 void** entry = nullptr;
 void** runtime = nullptr;
 const rpcs3::ios::aot::native_module_descriptor *native_descriptor = nullptr, *peer_descriptor = nullptr;
 if (use_jit)
 {
  jit = std::make_unique<jit_compiler>(std::unordered_map<std::string, u64>{}, jit_compiler::cpu(""), 1);
  require(jit->add(source + "/native-abi-jit.o"), "JIT could not load the native ELF object from identical IR");
  jit->fin();
  entry = reinterpret_cast<void**>(jit->get("rpcs3_aot_module_v2"));
  runtime = reinterpret_cast<void**>(jit->get("rpcs3_aot_register_runtime_v2"));
  native_descriptor = reinterpret_cast<const rpcs3::ios::aot::native_module_descriptor*>(jit->get("rpcs3_aot_native_module"));
  peer_jit = std::make_unique<jit_compiler>(std::unordered_map<std::string, u64>{}, jit_compiler::cpu(""), 1);
  require(peer_jit->add(source + "/native-abi-jit.o"), "peer JIT object load failed"); peer_jit->fin();
  peer_entry = reinterpret_cast<void**>(peer_jit->get("rpcs3_aot_module_v2"));
  peer_runtime = reinterpret_cast<void**>(peer_jit->get("rpcs3_aot_register_runtime_v2"));
  peer_descriptor = reinterpret_cast<const rpcs3::ios::aot::native_module_descriptor*>(peer_jit->get("rpcs3_aot_native_module"));
 }
 else
 {
  auto handle = dlopen((source + "/native-abi.dylib").c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!handle) throw std::runtime_error(dlerror());
  entry = static_cast<void**>(dlsym(handle, "rpcs3_aot_module_v2"));
  runtime = static_cast<void**>(dlsym(handle, "rpcs3_aot_register_runtime_v2"));
  native_descriptor = static_cast<const rpcs3::ios::aot::native_module_descriptor*>(dlsym(handle, "rpcs3_aot_native_module"));
  auto peer = dlopen((source + "/native-peer.dylib").c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!peer) throw std::runtime_error(dlerror());
  peer_entry = static_cast<void**>(dlsym(peer, "rpcs3_aot_module_v2"));
  peer_runtime = static_cast<void**>(dlsym(peer, "rpcs3_aot_register_runtime_v2"));
  peer_descriptor = static_cast<const rpcs3::ios::aot::native_module_descriptor*>(dlsym(peer, "rpcs3_aot_native_module"));
 }
 require(entry && runtime && *entry && peer_entry && peer_runtime && peer_entry[2] != entry[2], "native proof distinct-image exports missing");
 require(native_descriptor && peer_descriptor && native_descriptor->entry_count == 14 &&
     native_descriptor->entries[0].target == entry[0] && native_descriptor->entries[8].target == entry[1],
     "native descriptor lost compiled function identity");
 if (!use_jit)
 {
  for (auto descriptor : {native_descriptor, peer_descriptor})
  {
   std::vector<rpcs3::ios::aot::native_image_range> ranges;
   std::string error;
   require(rpcs3::ios::aot::inspect_native_image(descriptor, ranges, error), error);
   require(rpcs3::ios::aot::validate_native_module(descriptor, ranges, error), error);
   require(!rpcs3::ios::aot::validate_native_module(reinterpret_cast<const rpcs3::ios::aot::native_module_descriptor*>(1), ranges, error), "outside-image descriptor accepted");
   auto damaged = *descriptor;
   ranges.push_back({reinterpret_cast<std::uintptr_t>(&damaged), sizeof(damaged), true, true, false});
   ++damaged.version;
   require(!rpcs3::ios::aot::validate_native_module(&damaged, ranges, error), "wrong native version accepted");
   damaged = *descriptor; damaged.import_table = reinterpret_cast<void***>(descriptor->entries[0].target);
   require(!rpcs3::ios::aot::validate_native_module(&damaged, ranges, error), "executable import destination accepted");
   damaged = *descriptor; damaged.entries = reinterpret_cast<const rpcs3::ios::aot::native_module_entry*>(1);
   require(!rpcs3::ios::aot::validate_native_module(&damaged, ranges, error), "invalid entry table dereferenced");
   auto wrong_entry = descriptor->entries[0];
   ranges.push_back({reinterpret_cast<std::uintptr_t>(&wrong_entry), sizeof(wrong_entry), true, true, false});
   damaged = *descriptor; damaged.entries = &wrong_entry; damaged.entry_count = 1;
   wrong_entry.target = const_cast<rpcs3::ios::aot::native_module_descriptor*>(descriptor);
   require(!rpcs3::ios::aot::validate_native_module(&damaged, ranges, error), "data admitted as executable target");
   const char unknown[] = "unregistered native module import";
   const char* names[] = {unknown};
   ranges.push_back({reinterpret_cast<std::uintptr_t>(unknown), sizeof(unknown), true, false, false});
   ranges.push_back({reinterpret_cast<std::uintptr_t>(names), sizeof(names), true, false, false});
   damaged = *descriptor; damaged.imports = names; damaged.import_count = 1;
   require(!rpcs3::ios::aot::validate_native_module(&damaged, ranges, error), "unknown module import accepted");
   auto wrong_return = descriptor->returns[0];
   ranges.push_back({reinterpret_cast<std::uintptr_t>(&wrong_return), sizeof(wrong_return), true, true, false});
   damaged = *descriptor; damaged.returns = &wrong_return; damaged.return_count = 1;
   wrong_return.targets = reinterpret_cast<void***>(descriptor->entries[0].target);
   require(!rpcs3::ios::aot::validate_native_module(&damaged, ranges, error), "executable return destination accepted");
  }
  std::cout << "Native module contract: loaded-image bounds, permissions and version rejection passed\n";
 }
 *runtime = reinterpret_cast<void*>(&rpcs3_aot_native_exit);
 runtime[1] = reinterpret_cast<void*>(&rpcs3::ios::aot::native_import<&native_spu_escape_fixture>::call);
 runtime[3] = reinterpret_cast<void*>(&rpcs3::ios::aot::native_import<&native_spu_check_fixture>::call);
 runtime[2] = reinterpret_cast<void*>(&rpcs3::ios::aot::native_import<&native_helper_fixture>::call);
 std::vector<void*> program_targets(SPU_LS_SIZE / 4);
 runtime[4] = program_targets.data();
 const auto& native_symbols = ppu_native_runtime_symbols();
 require(native_symbols.size() == ppu_llvm_runtime_symbols().size() + 1 && native_symbols.contains("__aot_ppu_verify"),
     "native import catalog must add only the file-code verifier to shared JIT helpers");
 for (const auto& [name, pointer] : ppu_llvm_runtime_symbols())
  require(native_symbols.contains(name) && native_symbols.at(name), "missing native PPU helper: " + name);
 runtime[5] = reinterpret_cast<void*>(native_symbols.at("__check"));
 runtime[6] = reinterpret_cast<void*>(native_symbols.at("__trap"));
 std::copy_n(runtime, 7, peer_runtime);
 struct dispatch_state { std::vector<void*>& targets; u64 calls = 0; bool fail = false; } dispatch_state{program_targets};
 rpcs3::ios::aot::native_spu_dispatch spu_dispatch{&dispatch_state, [](void* owner, void* cpu, void*) -> void*
 {
  auto& state = *static_cast<struct dispatch_state*>(owner);
  ++state.calls;
  if (state.fail) throw std::runtime_error("native SPU resolver exception");
  return state.targets.at((static_cast<spu_thread*>(cpu)->pc & 0x3fffc) / 4);
 }};
 rpcs3::ios::aot::native_return_registry returns;
 std::array<rpcs3::ios::aot::native_loaded_module, 2> loaded_modules;
 std::array<std::vector<void*>, 2> import_tables;
 unsigned image_index = 0;
 for (auto descriptor : {native_descriptor, peer_descriptor})
 {
  if (!use_jit)
  {
   std::vector<rpcs3::ios::aot::native_image_range> ranges;
   std::string error;
   require(rpcs3::ios::aot::inspect_native_image(descriptor, ranges, error), error);
   auto& loaded = loaded_modules[image_index++];
   auto damaged = *descriptor; ++damaged.version;
   ranges.push_back({reinterpret_cast<std::uintptr_t>(&damaged), sizeof(damaged), true, true, false});
   const auto before = returns.size();
   auto* previous_imports = *descriptor->import_table;
   require(!loaded.admit(&damaged, ranges, returns, error) && returns.size() == before &&
       *descriptor->import_table == previous_imports, "failed native admission changed published state");
   require(loaded.admit(descriptor, ranges, returns, error), error);
   const auto admitted_size = returns.size();
   require(loaded.admit(descriptor, ranges, returns, error) && returns.size() == admitted_size,
       "repeated native admission duplicated return tokens");
   continue;
  }
  auto names = descriptor->imports;
  auto& slots = import_tables[image_index++];
  slots.resize(descriptor->import_count);
  for (unsigned i = 0; i < slots.size(); ++i)
  {
   const auto target = rpcs3::ios::aot::resolve_native_import(names[i]);
   require(target, std::string("Cold native helper registration missing: ") + names[i]);
   slots[i] = reinterpret_cast<void*>(target);
  }
  *descriptor->import_table = slots.data();
 }
 require(!rpcs3::ios::aot::resolve_native_import("unknown native helper"), "unknown native helper was accepted");

 vm::reserve_map(vm::main, 0x10000, 0x1fff0000, vm::page_64k_size);
 require(vm::falloc(0x10000, 0x40000), "native dispatch target allocation failed");
 ppu_register_range(0x10000, 0x40000);
 const auto interpreter_target = *reinterpret_cast<void**>(vm::g_exec_addr + 0x10040 * 2);
 rpcs3::ios::aot::native_ppu_table native_table;
 require(!native_table.register_range(1, 4) && !native_table.register_range(0xfffffffc, 8) &&
     !native_table.bind(0x10040, 0x10000, peer_entry[2]), "invalid/unmapped PPU table range accepted");
 require(native_table.register_range(0x10000, 0x40000), "native PPU dispatch allocation failed");
 auto* native_exec = native_table.base();
 require(native_exec != vm::g_exec_addr && native_table.target(0x10040) == reinterpret_cast<void*>(&rpcs3_aot_native_ppu_fallback),
     "native PPU dispatch mixed interpreter pointers or lacked fallback");
 require(!native_table.bind(0x10040, 0x10001, peer_entry[2]) && !native_table.bind(0x10040, 0x20000000, peer_entry[2]),
     "native PPU dispatch truncated segment identity");
 require(native_table.bind(0x10040, 0x10000, peer_entry[2]) && native_table.register_range(0x10000, 4) &&
     native_table.target(0x10040) == peer_entry[2], "overlapping registration erased a compiled target");
 require(native_table.invalidate(0x10040, 4) && native_table.target(0x10040) == reinterpret_cast<void*>(&rpcs3_aot_native_ppu_fallback),
     "native PPU invalidation retained compiled code");
 require(*reinterpret_cast<void**>(vm::g_exec_addr + 0x10040 * 2) == interpreter_target,
     "native PPU table changed interpreter dispatch");
 // Exactly the native indirect dispatch representation: an RW data pointer,
 // never an executable patch or generated trampoline.
 for (u32 pc : {0x20000u, 0x10014u, 0x10054u})
  *reinterpret_cast<void**>(native_exec + pc * 2) = reinterpret_cast<void*>(&rpcs3_aot_native_exit);
 id_manager::g_id = ppu_thread::id_base;
 auto thread = std::make_unique<ppu_thread>(ppu_thread_params{}, "Native ABI proof", 1000, -1);
 thread->hv_ctx.regs[0] = reinterpret_cast<u64>(&rpcs3_aot_native_exit);
 if (std::getenv("RPCS3_AOT_NATIVE_TRAP_TEST"))
 {
  g_cfg.core.stub_ppu_traps.set(0);
  thread->state.store({});
  const std::array<u64, 7> args = {reinterpret_cast<u64>(native_exec), reinterpret_cast<u64>(thread.get()), 0x10000, reinterpret_cast<u64>(vm::g_base_addr), 0, 0, 0};
  rpcs3::ios::aot::invoke_native(entry[8], args, thread.get(), thread->hv_ctx);
  throw std::runtime_error("Native fatal trap unexpectedly returned");
 }

 // Exercise the loader-partition compiler and normal admission independently
 // of the proof image's custom terminal helper bindings.
 auto ppu_pipeline_handle = dlopen((source + "/native-ppu-pipeline.dylib").c_str(), RTLD_NOW | RTLD_LOCAL);
 require(ppu_pipeline_handle, "Cannot load native PPU pipeline image");
 const auto* ppu_descriptor = static_cast<const rpcs3::ios::aot::native_module_descriptor*>(dlsym(ppu_pipeline_handle, "rpcs3_aot_native_module"));
 std::vector<rpcs3::ios::aot::native_image_range> ppu_ranges; std::string ppu_error;
 require(rpcs3::ios::aot::inspect_native_image(ppu_descriptor, ppu_ranges, ppu_error), ppu_error);
 rpcs3::ios::aot::native_return_registry ppu_returns;
 rpcs3::ios::aot::native_loaded_module ppu_loaded;
 require(ppu_loaded.admit(ppu_descriptor, ppu_ranges, ppu_returns, ppu_error), ppu_error);
 require(ppu_descriptor->entry_count == 8, "native PPU compiler lost whole functions");
 const auto verified_words = native_ppu_fixture();
 std::memcpy(vm::base(0x30000), verified_words.data(), verified_words.size() * 4);
 auto guard_context = std::make_unique<rpcs3::ios::aot::native_ppu_guard_context>();
 std::atomic<u64> inline_entries{0}, verification_misses{0};
 guard_context->pages = rpcs3::ios::aot::native_ppu_page_registry();
 guard_context->dispatch_epoch = ppu_aot_dispatch_epoch();
 guard_context->verified_entries = &inline_entries;
 guard_context->verification_misses = &verification_misses;
 require(!rpcs3::ios::aot::native_ppu_guard_key(0x100000002ull, 0x30000) &&
     !rpcs3::ios::aot::native_ppu_guard_key(2, 0x100000000ull) &&
     !rpcs3::ios::aot::native_ppu_guard_key(2, 0x30004) &&
     !rpcs3::ios::aot::native_ppu_guard_key(3, 0x30000), "native proof key truncated an epoch/relocation");
 thread->aot_native_context = guard_context.get();
 rpcs3::ios::aot::native_ppu_table pipeline_table;
 require(pipeline_table.register_range(0x30000, 0x20000), "native PPU pipeline table allocation failed");
 for (u32 n = 0; n < ppu_descriptor->entry_count; ++n)
  require(pipeline_table.bind(0x30000 + ppu_descriptor->entries[n].address, 0x30000, ppu_descriptor->entries[n].target),
      "native PPU pipeline table binding failed");
 for (u32 count : {1u, 4096u})
 {
  thread->state.store({}); thread->gpr[4] = 11; thread->gpr[5] = 17; thread->ctr = count; thread->lr = 0x40000;
  const std::array<u64, 7> call_args = {reinterpret_cast<u64>(pipeline_table.base()), reinterpret_cast<u64>(thread.get()),
      0x30000, reinterpret_cast<u64>(vm::g_base_addr), 0x12345, 0x23456, 0x34567};
  rpcs3::ios::aot::invoke_native(pipeline_table.target(0x30000), call_args, thread.get(), thread->hv_ctx);
  require(thread->cia == 0x40000 && thread->gpr[4] == 11 + u64(count) * 3 && thread->gpr[5] == 17 + u64(count) * 7 &&
      thread->gpr[0] == call_args[4] && thread->gpr[1] == call_args[5] && thread->gpr[2] == call_args[6],
      "native PPU pipeline relocated execution/fallback mismatch");
 }
 require(inline_entries.load() > 4096, "native PPU inline guards never executed");
 require(verification_misses.load() <= ppu_descriptor->entry_count,
     "warm native PPU entries repeated handler validation");
 // Another worker context can reuse the same entry proof. An epoch whose
 // low bits match must instead take the checked slow path, never wrap.
 const auto before_shared_misses = verification_misses.load();
 auto other_context = *guard_context;
 thread->aot_native_context = &other_context;
 const std::array<u64, 7> proof_args = {reinterpret_cast<u64>(pipeline_table.base()), reinterpret_cast<u64>(thread.get()),
     0x30000, reinterpret_cast<u64>(vm::g_base_addr), 0x12345, 0x23456, 0x34567};
 const auto run_proof_loop = [&]
 {
  thread->state.store({}); thread->gpr[4] = 11; thread->gpr[5] = 17; thread->ctr = 8; thread->lr = 0x40000;
  rpcs3::ios::aot::invoke_native(pipeline_table.target(0x30000), proof_args, thread.get(), thread->hv_ctx);
  require(thread->cia == 0x40000 && thread->gpr[4] == 35 && thread->gpr[5] == 73,
      "shared native proof changed execution");
 };
 run_proof_loop();
 require(verification_misses.load() == before_shared_misses, "entry proof was not shared across worker contexts");
 std::atomic<u64> overflow_epoch{ppu_aot_dispatch_epoch()->load() + (1ull << 32)};
 other_context.dispatch_epoch = &overflow_epoch;
 const auto before_overflow_inline = inline_entries.load();
 run_proof_loop();
 require(inline_entries.load() == before_overflow_inline && verification_misses.load() > before_shared_misses,
     "wrapped dispatch epoch reused a stale proof");
 thread->aot_native_context = guard_context.get();
 // A direct compiled call must check the callee even after its handler proof
 // has been cached; guest code bytes are never eligible for cached acceptance.
 auto* changed_words = static_cast<be_t<u32>*>(vm::base(0x30000));
 const auto original_callee = changed_words[2];
 changed_words[2] = ppu_instructions::ADDI(5, 5, 99);
 const std::array<u64, 7> guard_args = {reinterpret_cast<u64>(pipeline_table.base()), reinterpret_cast<u64>(thread.get()),
     0x30000, reinterpret_cast<u64>(vm::g_base_addr), 0x12345, 0x23456, 0x34567};
 const auto check_guard = [&]
 {
  thread->state.store({}); thread->gpr[4] = 11; thread->gpr[5] = 17; thread->ctr = 1; thread->lr = 0x40000;
  rpcs3::ios::aot::invoke_native(pipeline_table.target(0x30000), guard_args, thread.get(), thread->hv_ctx);
  require(thread->cia == 0x30008 && thread->gpr[4] == 14 && thread->gpr[5] == 17 && thread->gpr[0] == guard_args[4],
      "native direct callee bypassed changed-code/hook check or lost live state");
 };
 check_guard(); changed_words[2] = original_callee;
 auto* handler = reinterpret_cast<ppu_intrp_func_t*>(vm::g_exec_addr + 0x30008 * 2);
 const auto original_handler = *handler;
 ppu_register_function_at(0x30008, 4, reinterpret_cast<ppu_intrp_func_t>(&rpcs3_aot_native_exit));
 check_guard();
 ppu_register_function_at(0x30008, 4, original_handler);
 thread->state.store({}); thread->gpr[4] = 11; thread->gpr[5] = 17; thread->ctr = 1; thread->lr = 0x40000;
 rpcs3::ios::aot::invoke_native(pipeline_table.target(0x30000), guard_args, thread.get(), thread->hv_ctx);
 require(thread->cia == 0x40000 && thread->gpr[5] == 24, "restored code/hooks did not resume native execution");
 // Mapping permission changes need no new cache protocol: generated guards
 // read the native page registry before touching any guest instruction bytes.
 thread->state += cpu_flag::wait;
 require(vm::page_protect(0x30000, 0x10000, 0, 0, vm::page_executable), "cannot alter guard fixture permission");
 thread->state.store({}); thread->cia = 0x30000; thread->gpr[4] = 11;
 rpcs3::ios::aot::invoke_native(pipeline_table.target(0x30000), guard_args, thread.get(), thread->hv_ctx);
 require(thread->cia == 0x30000 && thread->gpr[4] == 11, "cached native guard accepted non-executable memory");
 thread->state += cpu_flag::wait;
 require(vm::page_protect(0x30000, 0x10000, 0, vm::page_executable), "cannot restore guard fixture permission");
 thread->aot_native_context = nullptr;
 std::cout << "Native PPU guard: changed direct callee, hook replacement, live registers and restoration passed\n";
 std::cout << "Native PPU pipeline: loader partition, emission, admission, relocation and fallback passed\n";

 std::ofstream times(directory + "/native-abi-times.csv");
 times << "mode,iterations,seconds\n";
 for (u32 iterations : {1u, 2u, 31u, 4096u, 1000000u, 64000000u, 64000000u, 64000000u, 64000000u, 64000000u, 64000000u, 64000000u})
 {
  thread->state.store({}); thread->gpr[4] = 11; thread->gpr[5] = 17; thread->ctr = iterations; thread->lr = 0x20000;
  const std::array<u64, 7> args = {reinterpret_cast<u64>(native_exec), reinterpret_cast<u64>(thread.get()), 0x10000, reinterpret_cast<u64>(vm::g_base_addr), 0, 0, 0};
  const auto start = std::chrono::steady_clock::now();
  rpcs3::ios::aot::invoke_native(*entry, args, thread.get(), thread->hv_ctx);
  const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  require(thread->gpr[4] == 11 + u64(iterations) * 3 && thread->gpr[5] == 17 + u64(iterations) * 7 && thread->ctr == 0 && thread->cia == 0x20000,
      "native cross-function execution mismatch");
  times << (use_jit ? "jit" : "aot") << "," << iterations << "," << std::setprecision(12) << elapsed << "\n";
  std::cout << "Native ABI proof: iterations=" << iterations << " seconds=" << elapsed << '\n';
 }
 thread->state.store({}); thread->gpr[4] = 11; thread->gpr[5] = 17; thread->ctr = 1; thread->lr = 0x20004;
 const std::array<u64, 7> missing_args = {reinterpret_cast<u64>(native_exec), reinterpret_cast<u64>(thread.get()),
     0x10000, reinterpret_cast<u64>(vm::g_base_addr), 0x123456789ull, 0xabcdef123ull, 0x987654321ull};
 rpcs3::ios::aot::invoke_native(*entry, missing_args, thread.get(), thread->hv_ctx);
 require(thread->cia == 0x20004 && thread->gpr[4] == 14 && thread->gpr[5] == 24 &&
     thread->gpr[0] == missing_args[4] && thread->gpr[1] == missing_args[5] && thread->gpr[2] == missing_args[6],
     "native indirect missing-target fallback lost PPU state");
 thread->state.store({}); thread->ctr = 31; thread->lr = 0x20000;
 const std::array<u64, 7> canary_args = {reinterpret_cast<u64>(native_exec), reinterpret_cast<u64>(thread.get()), 0x10000, reinterpret_cast<u64>(vm::g_base_addr), 0, 0, 0};
 const auto canary_hypervisor = thread->hv_ctx;
 require(rpcs3_aot_test_registers(*entry, canary_args.data(), thread->hv_ctx.regs), "native gateway corrupted Darwin callee-saved registers");
 thread->hv_ctx = canary_hypervisor;
 std::cout << "Native ABI canaries: x18, x19-x29 and d8-d15 preserved\n";
 {
  auto fallback_args = canary_args;
  fallback_args[4] = 0x123456789abcdef0ull;
  fallback_args[5] = 0xfedcba9876543210ull;
  fallback_args[6] = 0xabcdef0123456789ull;
  thread->cia = 0x23450;
  rpcs3::ios::aot::invoke_native(reinterpret_cast<void*>(&rpcs3_aot_native_ppu_fallback),
      fallback_args, thread.get(), thread->hv_ctx);
  require(thread->cia == 0x23450 && thread->gpr[0] == fallback_args[4] &&
      thread->gpr[1] == fallback_args[5] && thread->gpr[2] == fallback_args[6],
      "native PPU fallback lost live register arguments or CIA");
 }
 thread->state.store(cpu_flag::stop); thread->gpr[4] = 101; thread->gpr[5] = 103; thread->ctr = 123;
 const std::array<u64, 7> args = {reinterpret_cast<u64>(native_exec), reinterpret_cast<u64>(thread.get()), 0x10000, reinterpret_cast<u64>(vm::g_base_addr), 0, 0, 0};
 rpcs3::ios::aot::invoke_native(*entry, args, thread.get(), thread->hv_ctx);
 require(thread->gpr[4] == 101 && thread->gpr[5] == 103 && thread->ctr == 123 && thread->cia == 0x10000,
     "native pending-state exit executed guest work or lost PC");
 thread->state.store({}); thread->lr = 0x20000;
 const auto saved_hypervisor = thread->hv_ctx;
 nested_entry = *entry; nested_cpu = thread.get(); nested_arguments = args;
 for (int mode : {0, 1, 2})
 {
  helper_mode = mode; helper_destructors = 0;
  thread->gpr[7] = 999; thread->gpr[8] = 998; thread->ctr = 3;
  bool caught = false;
  try { rpcs3::ios::aot::invoke_native(entry[2], args, thread.get(), thread->hv_ctx); }
  catch (const std::runtime_error& error) { caught = std::string_view(error.what()) == "native helper exception proof"; }
  require(caught == (mode == 1) && helper_destructors == 1, "native helper cleanup/exception transfer failed");
  require(thread->gpr[7] == (mode == 1 ? 999u : 71u) && thread->gpr[8] == (mode == 1 ? 998u : 76u),
      "native helper return/exception committed incorrect registers");
  require(!std::memcmp(&saved_hypervisor, &thread->hv_ctx, sizeof(saved_hypervisor)), "nested native gateway lost hypervisor context");
  require(!rpcs3::ios::aot::escape_native_if_active(thread.get()), "native gateway left an active invocation");
 }
 runtime[2] = reinterpret_cast<void*>(native_symbols.at("__get_tb"));
 thread->gpr[7] = 0; thread->gpr[8] = 0;
 rpcs3::ios::aot::invoke_native(entry[2], args, thread.get(), thread->hv_ctx);
 require(thread->gpr[7] && thread->gpr[8] == thread->gpr[7] + 5, "native timebase import failed");
 runtime[2] = reinterpret_cast<void*>(&rpcs3::ios::aot::native_import<&native_helper_fixture>::call);
 std::cout << "Native ABI imports: shared PPU catalog, state check and timebase passed\n";
 helper_mode = 0;
 std::cout << "Native ABI helpers: returning calls, nested gateway, exception cleanup and context restoration passed\n";
 // Link a target in a distinct immutable image through native dispatch data.
 require(native_table.bind(0x10040, 0x10000, peer_entry[2]), "native peer binding");
 thread->gpr[7] = 0; thread->gpr[8] = 0;
 rpcs3::ios::aot::invoke_native(entry[3], args, thread.get(), thread->hv_ctx);
 require(thread->gpr[7] == 71 && thread->gpr[8] == 76 && thread->cia == 0x20000, "native cross-image dispatch failed");
 std::cout << "Native ABI linking: external PPU target in a distinct image through RW dispatch data passed\n";
 auto relocated_args = args; relocated_args[2] = 0x30000;
 thread->gpr[4] = 11; thread->gpr[5] = 17; thread->ctr = 31;
 rpcs3::ios::aot::invoke_native(entry[0], relocated_args, thread.get(), thread->hv_ctx);
 require(thread->gpr[4] == 104 && thread->gpr[5] == 234 && thread->cia == 0x20000,
     "native PPU relocated function execution mismatch");
 thread->state.store(cpu_flag::stop);
 rpcs3::ios::aot::invoke_native(entry[0], relocated_args, thread.get(), thread->hv_ctx);
 require(thread->cia == 0x30000, "native PPU relocated state exit lost PC");
 thread->state.store({});
 require(native_table.bind(0x30040, 0x30000, peer_entry[2]), "relocated native peer binding");
 thread->gpr[7] = 0; thread->gpr[8] = 0;
 rpcs3::ios::aot::invoke_native(entry[3], relocated_args, thread.get(), thread->hv_ctx);
 require(thread->gpr[7] == 71 && thread->gpr[8] == 76 && thread->cia == 0x20000,
     "native PPU relocated external target mismatch");
 std::cout << "Native ABI relocation: PPU functions, state exits and external targets passed\n";

 ppu_interpreter_rt ppu_interpreter;
 std::mt19937_64 random(0xa07b1);
 for (unsigned sample = 0; sample < 128; ++sample)
 {
  const u64 value = random();
  vm::write64(0x28020, value);
  thread->gpr[3] = 0x28000;
  rpcs3::ios::aot::invoke_native(entry[14], args, thread.get(), thread->hv_ctx);
  require(thread->gpr[4] == value && thread->gpr[5] == u32(value) && vm::read32(0x28050) == u32(value),
      "native PPU memory/endian result mismatch");
  thread->fpr[5] = double(s64(random() % 200000) - 100000) / 128.0;
  thread->fpr[6] = double(s64(random() % 200000) - 100000) / 128.0;
  thread->cia = 0x10070;
  vm::write32(0x10070, 0xfc85302au);
  ppu_intrp_func end{+[](ppu_thread& cpu, ppu_opcode_t, be_t<u32>* next, ppu_intrp_func*) { cpu.cia = vm::get_addr(next); }};
  ppu_interpreter.decode(0xfc85302au)(*thread, {0xfc85302au}, vm::get_super_ptr<be_t<u32>>(0x10070), &end);
  const auto expected = thread->fpr[4];
  thread->fpr[4] = 0;
  rpcs3::ios::aot::invoke_native(entry[15], args, thread.get(), thread->hv_ctx);
  require(!std::memcmp(&expected, &thread->fpr[4], sizeof(expected)), "native PPU floating result differs from interpreter");
 }
 id_manager::g_id = spu_thread::id_base;
 auto spu = std::make_unique<spu_thread>(nullptr, 0, "Native ABI SPU proof", 0);
 spu_thread::map_ls(*spu->shm, spu->ls);
 auto ls = spu->ls;
 // Native SPU local store is big endian; retain exactly the compiled identity.
 auto code = reinterpret_cast<be_t<u32>*>(ls);
 code[0x100 / 4] = spu_instructions::AI(4, 4, 3);
 code[0x104 / 4] = spu_instructions::AI(5, 5, -1);
 code[0x108 / 4] = spu_instructions::BRNZ(5, -8);
 code[0x10c / 4] = (0x35000000u | (6u << 7));
 const std::array<u64, 7> spu_args = {reinterpret_cast<u64>(spu.get()), reinterpret_cast<u64>(ls), 0, 0, 0, 0, 0};
 for (u32 count : {1u, 2u, 31u, 4096u, 1000000u})
 {
  spu->state.store({}); spu->pc = 0x100;
  for (int lane = 0; lane < 4; ++lane) { spu->gpr[4]._u32[lane] = 11; spu->gpr[5]._u32[lane] = count; }
  spu->gpr[6]._u32[3] = 0x2000;
  rpcs3::ios::aot::invoke_native(entry[1], spu_args, spu.get(), spu->hv_ctx, &spu_dispatch);
  require(spu->gpr[4]._u32[3] == 11 + 3 * count && spu->gpr[5]._u32[3] == 0 && spu->pc == 0x2000,
      "native SPU loop/indirect exit mismatch");
 }
 dispatch_state.fail = true;
 const auto saved_spu_hypervisor = spu->hv_ctx;
 spu->pc = 0x100; spu->gpr[5]._u32[3] = 1;
 bool resolver_threw = false;
 try { rpcs3::ios::aot::invoke_native(entry[1], spu_args, spu.get(), spu->hv_ctx, &spu_dispatch); }
 catch (const std::runtime_error& error) { resolver_threw = std::string_view(error.what()) == "native SPU resolver exception"; }
 dispatch_state.fail = false;
 require(resolver_threw && !std::memcmp(&saved_spu_hypervisor, &spu->hv_ctx, sizeof(saved_spu_hypervisor)) &&
     !rpcs3::ios::aot::escape_native_if_active(spu.get()), "native SPU resolver failed exception/context restoration");
 std::cout << "Native ABI SPU dispatch: prebuilt bridge and resolver exception restoration passed\n";
 spu->state.store(cpu_flag::stop); spu->pc = 0x100; spu->gpr[4]._u32[3] = 123;
 rpcs3::ios::aot::invoke_native(entry[1], spu_args, spu.get(), spu->hv_ctx, &spu_dispatch);
 require(spu->gpr[4]._u32[3] == 123 && spu->pc == 0x100, "native SPU pending-state exit failed");
 spu->state.store({}); code[0x100 / 4] = spu_instructions::AI(4, 4, 9);
 rpcs3::ios::aot::invoke_native(entry[1], spu_args, spu.get(), spu->hv_ctx, &spu_dispatch);
 require(spu->gpr[4]._u32[3] == 123 && spu->pc == 0x100, "native SPU modified-code rejection failed");
 spu_opcode_t rd{0xdu << 21}; rd.ra = SPU_RdInMbox; rd.rt = 23;
 code[0x140 / 4] = rd.opcode; code[0x144 / 4] = 0x35000000u | (6u << 7);
 for (u32 value : {0u, 0x12345678u, 0xffffffffu})
 {
  spu->pc = 0x140; spu->gpr[23] = v128{};
  spu->ch_in_mbox.push(value);
  rpcs3::ios::aot::invoke_native(entry[11], spu_args, spu.get(), spu->hv_ctx, &spu_dispatch);
  require(spu->pc == 0x2000 && spu->gpr[23]._u32[3] == value && spu->ch_in_mbox.get_count() == 0,
      "native SPU mailbox helper did not consume exactly one value");
 }
 std::cout << "Native ABI SPU imports: cold registered native mailbox helper passed\n";
 std::cout << "Native ABI SPU proof: loop, indirect exit, pending state and native code verification passed\n";
 spu_interpreter_rt spu_interpreter;
 std::fenv_t floating_environment;
 std::fegetenv(&floating_environment);
 std::fesetround(FE_TOWARDZERO); gv_set_zeroing_denormals();
 spu_opcode_t fa{0x2c4u << 21}; fa.rt = 4; fa.ra = 5; fa.rb = 6;
 code[0x180 / 4] = fa.opcode; code[0x184 / 4] = 0x35000000u | (127u << 7);
 code[0x1c0 / 4] = spu_instructions::LQD(4, 3, 32);
 code[0x1c4 / 4] = spu_instructions::STQD(4, 3, 64);
 code[0x1c8 / 4] = 0x35000000u | (127u << 7);
 spu->gpr[127]._u32[3] = 0x2000;
 for (unsigned sample = 0; sample < 128; ++sample)
 {
  for (u32 lane = 0; lane < 4; ++lane)
  {
   spu->gpr[5]._f[lane] = float(s64(random() % 20000) - 10000) / 128.f;
   spu->gpr[6]._f[lane] = float(s64(random() % 20000) - 10000) / 128.f;
  }
  spu->pc = 0x180;
  spu_interpreter.decode(fa.opcode)(*spu, fa);
  const auto expected = spu->gpr[4];
  spu->gpr[4] = {};
  rpcs3::ios::aot::invoke_native(entry[16], spu_args, spu.get(), spu->hv_ctx, &spu_dispatch);
  require(!std::memcmp(&expected, &spu->gpr[4], sizeof(expected)), "native SPU floating result differs from interpreter");
  const auto value = v128::from64(random(), random());
  std::memcpy(ls + 0x8020, &value, sizeof(value));
  std::memset(ls + 0x8040, 0, sizeof(value));
  spu->gpr[3] = v128::from32p(0x8000); spu->pc = 0x1c0;
  rpcs3::ios::aot::invoke_native(entry[17], spu_args, spu.get(), spu->hv_ctx, &spu_dispatch);
  require(!std::memcmp(ls + 0x8040, &value, sizeof(value)), "native SPU memory result mismatch");
 }
 std::fesetenv(&floating_environment);
 std::cout << "Native ABI arithmetic: 512 PPU/SPU floating and memory comparisons passed\n";
 // Token zero is never callable. Register all image-local return chunks in
 // one process-owned RW table, while keeping their machine code immutable.
 const auto binding_view = [](void** image, int index)
 {
  auto metadata = static_cast<void**>(image[index]);
  return rpcs3::ios::aot::native_return_binding{static_cast<u32*>(metadata[0]), static_cast<u32*>(metadata[1]),
      static_cast<void***>(metadata[2]), {static_cast<void**>(metadata[3]), *static_cast<u32*>(metadata[4])}};
 };
 if (use_jit) for (auto image : {entry, peer_entry})
 {
  const std::array bindings = {binding_view(image, 6), binding_view(image, 7)};
  require(returns.append(bindings), "native return registration failed");
 }
 const auto before_size = returns.size();
 require(returns.append({}) && returns.size() == before_size, "empty native registration changed return tokens");
 const std::array duplicate = {binding_view(entry, 6)};
 require(!returns.append(duplicate) && returns.size() == before_size, "duplicate native binding changed registration");
 u32 bad_base = 999, bad_count = 999; void** bad_table = nullptr;
 void* null_target = nullptr;
 const std::array invalid = {rpcs3::ios::aot::native_return_binding{&bad_base, &bad_count, &bad_table, {&null_target, 1}}};
 require(!returns.append(invalid) && bad_base == 999 && bad_count == 999 && !bad_table && returns.size() == before_size,
     "invalid native registration modified published state");
 spu_opcode_t branch{0x66u << 23}; branch.rt = 0; branch.si16 = 8;
 code[0x100 / 4] = branch.opcode;
 code[0x104 / 4] = spu_instructions::AI(20, 20, 3);
 code[0x108 / 4] = spu_instructions::AI(21, 21, -1);
 code[0x10c / 4] = spu_instructions::BRNZ(21, -12);
 code[0x110 / 4] = 0x35000000u | (6u << 7);
 code[0x120 / 4] = spu_instructions::AI(22, 22, 1);
 code[0x124 / 4] = 0x35000000u;
 const auto dispatches = [&] { return dispatch_state.calls; };
 for (bool reverse : {false, true}) for (u32 count : {1u, 31u, 4096u})
 {
  program_targets[0x120 / 4] = (reverse ? entry : peer_entry)[5];
  spu->state.store({}); spu->pc = 0x100;
  spu->gpr[1]._u32[3] = 0x1000;
  for (int lane = 0; lane < 4; ++lane)
  { spu->gpr[20]._u32[lane] = 11; spu->gpr[21]._u32[lane] = count; spu->gpr[22]._u32[lane] = 0; }
  spu->gpr[6]._u32[3] = 0x2000;
  const auto before_dispatch = dispatches();
  rpcs3::ios::aot::invoke_native((reverse ? peer_entry : entry)[4], spu_args, spu.get(), spu->hv_ctx, &spu_dispatch);
  require(dispatches() - before_dispatch <= 2, "native cached branch reentered the resolver in its loop");
  require(spu->gpr[20]._u32[3] == 11 + 3 * count && spu->gpr[21]._u32[3] == 0 &&
      spu->gpr[22]._u32[3] == count && spu->pc == 0x2000, "native SPU cross-image return failed");
 }
 // The main caller has cached the peer callee. Changing that callee's code
 // must fail native entry verification, clear the slot, and execute no work.
 program_targets[0x120 / 4] = peer_entry[5];
 code[0x120 / 4] = spu_instructions::AI(22, 22, 9);
 spu->pc = 0x100; spu->gpr[21]._u32[3] = 1; spu->gpr[22]._u32[3] = 0;
 const auto before_mismatch = dispatches();
 rpcs3::ios::aot::invoke_native(entry[4], spu_args, spu.get(), spu->hv_ctx, &spu_dispatch);
 require(spu->pc == 0x120 && !spu->gpr[22]._u32[3] && dispatches() == before_mismatch,
     "cached branch executed changed SPU code");
 code[0x120 / 4] = spu_instructions::AI(22, 22, 1);
 spu->pc = 0x100;
 rpcs3::ios::aot::invoke_native(entry[4], spu_args, spu.get(), spu->hv_ctx, &spu_dispatch);
 require(spu->pc == 0x2000 && spu->gpr[22]._u32[3] == 1 && dispatches() == before_mismatch + 2,
     "native branch verification failure did not reset its data slot");
 std::cout << "Native ABI SPU branches: cached data targets bypass lookup; changed-code reset passed\n";
 auto caller_metadata = static_cast<void**>(entry[6]);
 auto callee_metadata = static_cast<void**>(peer_entry[7]);
 auto& caller_base = *static_cast<u32*>(caller_metadata[0]);
 const auto valid_base = caller_base;
 program_targets[0x120 / 4] = peer_entry[5];
 for (u32 bad_token : {0u, returns.size()})
 {
  caller_base = bad_token;
  spu->pc = 0x100;
  for (int lane = 0; lane < 4; ++lane)
  { spu->gpr[20]._u32[lane] = 11; spu->gpr[21]._u32[lane] = 31; spu->gpr[22]._u32[lane] = 0; }
  rpcs3::ios::aot::invoke_native(entry[4], spu_args, spu.get(), spu->hv_ctx, &spu_dispatch);
  require(spu->pc == 0x104 && spu->gpr[20]._u32[3] == 11 && spu->gpr[21]._u32[3] == 31 &&
      spu->gpr[22]._u32[3] == 1, "invalid native return token executed a target");
 }
 caller_base = valid_base;
 auto return_targets = *static_cast<void***>(callee_metadata[2]);
 auto saved_target = return_targets[valid_base];
 return_targets[valid_base] = nullptr;
 spu->pc = 0x100;
 rpcs3::ios::aot::invoke_native(entry[4], spu_args, spu.get(), spu->hv_ctx, &spu_dispatch);
 require(spu->pc == 0x104 && spu->gpr[20]._u32[3] == 11, "null return target executed a chunk");
 return_targets[valid_base] = saved_target;
 *static_cast<void***>(callee_metadata[2]) = nullptr;
 spu->pc = 0x100;
 rpcs3::ios::aot::invoke_native(entry[4], spu_args, spu.get(), spu->hv_ctx, &spu_dispatch);
 require(spu->pc == 0x104 && spu->gpr[20]._u32[3] == 11, "missing return table executed a target");
 *static_cast<void***>(callee_metadata[2]) = return_targets;
 // Change the return instruction after the mirror was populated, without
 // changing the callee. The native opcode check must reject the stale chunk.
 auto& mirror = spu->stack_mirror[0x1000 / sizeof(v128)];
 u32 old_opcode; std::memcpy(&old_opcode, code + 0x104 / 4, sizeof(old_opcode));
 mirror._u64[0] = u64(valid_base) << 32 | old_opcode;
 mirror._u64[1] = u64(0x100) << 32 | 0x104;
 code[0x104 / 4] = spu_instructions::AI(20, 20, 9);
 spu->gpr[0]._u32[3] = 0x104; spu->pc = 0x120;
 rpcs3::ios::aot::invoke_native(peer_entry[5], spu_args, spu.get(), spu->hv_ctx, &spu_dispatch);
 require(spu->pc == 0x104 && spu->gpr[20]._u32[3] == 11, "changed return opcode executed a stale chunk");
 std::cout << "Native ABI SPU linking: cross-image calls, token returns and invalid-target rejection passed\n";
 // Exercise the same emitter, descriptor admission and selection routine
 // called by game preparation/loading, rather than the proof target map.
 auto pipeline_handle = dlopen((source + "/native-pipeline.dylib").c_str(), RTLD_NOW | RTLD_LOCAL);
 require(pipeline_handle, dlerror() ? "Cannot load native pipeline image" : "Missing native pipeline image");
 const auto* pipeline_descriptor = static_cast<const rpcs3::ios::aot::native_module_descriptor*>(dlsym(pipeline_handle, "rpcs3_aot_native_module"));
 auto pipeline_legacy = reinterpret_cast<const rpcs3_aot_module_v2* (*)()>(dlsym(pipeline_handle, "rpcs3_aot_module_v2"));
 require(pipeline_legacy, "native pipeline identity export missing");
 std::vector<rpcs3::ios::aot::native_image_range> pipeline_ranges; std::string pipeline_error;
 require(rpcs3::ios::aot::inspect_native_image(pipeline_descriptor, pipeline_ranges, pipeline_error), pipeline_error);
 rpcs3::ios::aot::native_return_registry pipeline_returns;
 rpcs3::ios::aot::native_loaded_module pipeline_loaded;
 require(pipeline_loaded.admit(pipeline_descriptor, pipeline_ranges, pipeline_returns, pipeline_error), pipeline_error);
 require(pipeline_descriptor->entry_count == 1 && pipeline_legacy()->entry_count == 1 && !pipeline_legacy()->entries[0].invoke,
     "native pipeline also exposed a fragment entry");
 const rpcs3::ios::aot::native_spu_selection selection{pipeline_legacy()->entries[0], pipeline_descriptor->entries[0].target};
 rpcs3::ios::aot::select_entries({});
 require(rpcs3::ios::aot::select_native_spu_entries(std::span(&selection, 1)), "native pipeline selection failed");
 code[0x100 / 4] = spu_instructions::AI(4, 4, 3);
 code[0x104 / 4] = spu_instructions::AI(5, 5, -1);
 code[0x108 / 4] = spu_instructions::BRNZ(5, -8);
 code[0x10c / 4] = 0x35000000u | (6u << 7);
 for (u32 count : {1u, 4096u})
 {
  spu->state.store({}); spu->pc = 0x100; spu->gpr[4] = v128::from32p(11); spu->gpr[5] = v128::from32p(count);
  spu->gpr[6]._u32[3] = 0x2000;
  require(rpcs3::ios::aot::execute_native_spu(*spu, ls), "production native SPU entry was not selected");
  require(spu->pc == 0x2000 && spu->gpr[4]._u32[3] == 11 + count * 3 && !spu->gpr[5]._u32[3], "production native SPU loop diverged");
 }
 code[0x104 / 4] = spu_instructions::AI(5, 5, -2); spu->pc = 0x100;
 require(!rpcs3::ios::aot::execute_native_spu(*spu, ls), "production native selector accepted modified program");
 require(rpcs3::ios::aot::statistics().native_spu_invocations == 2, "native execution count lost its invocation units");
 rpcs3::ios::aot::select_entries({});
 require(!rpcs3::ios::aot::execute_native_spu(*spu, ls), "native selection survived session clear");
 std::cout << "Native SPU pipeline: prepare, admit, select, execute and changed-code fallback passed\n";
 if (use_jit)
 {
  // Exercise the ordinary JIT builder, not file IR relocated by MCJIT.
  // Its verification dispatcher can return after repairing a patchpoint.
  // Misclassifying that C boundary as non-returning GHC inserts brk #0x99.
  code[0x104 / 4] = spu_instructions::AI(5, 5, -1);
  g_fxo->need<spu_runtime>();
  g_fxo->need<spu_cache>();
  spu->jit = spu_recompiler_base::make_llvm_recompiler(0);
  spu->jit->init();
  auto program = spu->jit->analyse(code, 0x100);
  auto target = spu->jit->compile(std::move(program));
  require(target, "ordinary SPU JIT test compilation failed");
  auto& dispatcher = (*spu_runtime::g_dispatcher)[spu->_ref<nse_t<u32>>(0x100) >> 12];
  const auto previous = dispatcher.exchange(target);
  code[0x104 / 4] = spu_instructions::AI(5, 5, -2);
  spu->state.store({}); spu->pc = 0x100; spu->block_counter = 1; spu->block_recover = 0;
  rpcs3::ios::aot::invoke_native(reinterpret_cast<void*>(target), spu_args, spu.get(), spu->hv_ctx);
  require(spu->pc == 0x100 && spu->block_recover == 1,
      "ordinary SPU JIT verification return lost its gateway state");
  dispatcher = previous;
  // Verification-disabled dispatch cannot use a writable branch patchpoint.
  // Its __spu-null path must handle tr_all returning from an unknown target.
  const bool verification = g_cfg.core.spu_verification.get();
  g_cfg.core.spu_verification.set(false);
  code[0x300 / 4] = spu_instructions::AI(4, 4, 7);
  code[0x304 / 4] = 0x35000000u | (6u << 7);
  code[0x200 / 4] = spu_instructions::AI(7, 7, 1);
  auto indirect_program = spu->jit->analyse(code, 0x300);
  auto indirect_target = spu->jit->compile(std::move(indirect_program));
  require(indirect_target, "ordinary indirect SPU JIT compilation failed");
  auto& indirect_slot = (*spu_runtime::g_dispatcher)[spu->_ref<nse_t<u32>>(0x200) >> 12];
  const auto indirect_previous = indirect_slot.exchange(spu_runtime::tr_dispatch);
  spu->gpr[4]._u32[3] = 0; spu->gpr[6]._u32[3] = 0x200;
  spu->state.store({}); spu->pc = 0x300; spu->block_counter = 1; spu->block_recover = 0;
  rpcs3::ios::aot::invoke_native(reinterpret_cast<void*>(indirect_target), spu_args, spu.get(), spu->hv_ctx);
  require(spu->pc == 0x200 && spu->gpr[4]._u32[3] == 7 && spu->block_recover != 0,
      "ordinary SPU JIT indirect dispatcher return lost guest state");
  indirect_slot = indirect_previous;
  g_cfg.core.spu_verification.set(verification);
  std::cout << "Ordinary SPU JIT: verification and indirect dispatch returned through gateway\n";
 }
 require(native_table.register_range(0xfffffffc, 4) && native_table.target(0xfffffffc) &&
     native_table.invalidate(0xffff0000, 0x10000), "last guest page overflowed native PPU table");
 native_table.reset();
 require(!native_table.target(0x10040) && !native_table.target(0xfffffffc), "native PPU reset retained a target");
 require(native_table.register_range(0x10040, 4) &&
     native_table.target(0x10040) == reinterpret_cast<void*>(&rpcs3_aot_native_ppu_fallback),
     "native PPU reboot restored stale compiled code");
 require(*reinterpret_cast<void**>(vm::g_exec_addr + 0x10040 * 2) == interpreter_target,
     "native PPU lifecycle changed interpreter dispatch");
 std::cout << "Native PPU table: isolated dispatch, relocation, missing-target fallback, invalidation and reset passed\n";
 if (!use_jit && directory == source)
 {
  const auto loader_words = native_ppu_fixture();
  std::memcpy(vm::base(0x30000), loader_words.data(), loader_words.size() * 4);
  ppu_register_range(0x30000, 0x20000);
  ppu_module<lv2_obj> info;
  info.is_relocatable = true; info.local_bounds = {0x30000, 0x30080};
  info.segs.push_back({0x30000, 128, 1, 5, 128, vm::base(0x30000)});
  info.addr_to_seg_index.emplace(0x30000, 0);
  for (auto [offset, size] : {std::pair{0u, 8u}, {8u, 8u}, {16u, 4u}, {32u, 12u}, {48u, 4u}, {80u, 8u}, {96u, 16u}, {112u, 8u}})
  {
   ppu_function function; function.addr = 0x30000 + offset; function.size = size; info.funcs.push_back(function);
  }
  // These manually driven CPU objects have no scheduler to acknowledge a
  // safepoint. Mark the idle fixtures waiting before exercising publication.
  thread->state += cpu_flag::wait; spu->state += cpu_flag::wait;
  rpcs3::ios::aot::test_native_ppu_loader(info, *thread, directory);
 }
 if (!use_jit) require(rpcs3::ios::jit::rejected_executable_requests() == 0 && rpcs3::ios::jit::rejected_preparation_requests() == 0,
     "native ABI file proof requested JIT");
 std::cout << "Native ABI " << (use_jit ? "JIT" : "file") << " proof: native PPU tail transfers and pending-state exit passed\n";
 // Keep the loaded executable image for process lifetime, as production does.
}
