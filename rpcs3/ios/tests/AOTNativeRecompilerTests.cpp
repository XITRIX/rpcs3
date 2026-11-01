#include "ios/AOTCompiler.h"
#include "ios/AOTPreparation.h"
#include "ios/AOTError.h"
#include "ios/AOTRecompiler.h"
#include "ios/AOTDispatch.h"
#include "ios/AOTCodeMatch.h"
#include "ios/AOTRuntime.h"
#include "ios/IOSStaticInterpreter.h"
#include "ios/AOTAnalysis.h"
#include "ios/RPCS3IOS.h"
#include "Emu/Cell/lv2/sys_sync.h"
#include "Emu/Cell/PPUThread.h"
#include "Emu/Cell/PPUInterpreter.h"
#include "Emu/Cell/SPUThread.h"
#include "Emu/Cell/SPUInterpreter.h"
#include "Emu/Cell/SPUInterpreterDiagnostics.h"
#include "Emu/Cell/SPURecompiler.h"
#include "Emu/Audio/Null/NullAudioBackend.h"
#include "util/simd.hpp"
#include <cfenv>
#include "Emu/Memory/vm.h"
#include "Emu/system_config.h"
#include "Utilities/JITIOS.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <set>
#include <dlfcn.h>
#include <chrono>
#include <signal.h>

void test_aot_native_analysis();
void test_aot_native_abi(const std::string&);
void test_aot_native_abi_load(const std::string&, const std::string&, bool);
extern void ppu_register_range(u32, u32);
extern void ppu_register_function_at(u32, u32, ppu_intrp_func_t);
extern bool ppu_form_branch_to_code(u32, u32, bool, bool);
extern bool ppu_breakpoint(u32, bool);
void test_aot_native_spu_cache(const std::string& output);
void test_aot_native_analysis_after_stop();
void test_aot_native_game_analysis(const std::string& file);

namespace rpcs3::ios::aot { void test_native_aot_game_inventory(const std::string&, const std::string&); void test_native_aot_admission(const std::string&, const std::string&, std::span<const instruction>); void test_native_aot_digest_table(const std::string& output); void test_native_aot_inventory_history(const std::string& output); std::vector<instruction> test_native_aot_inventory(const std::string& file); std::string test_native_aot_inventory_digest(std::span<const instruction>); }
namespace aot = rpcs3::ios::aot;
namespace path = std::filesystem;
static void check(bool value, const std::string& error) { if (!value) throw std::runtime_error(error); }
static aot::instruction unit(u32 kind, std::initializer_list<u32> words)
{
    check(words.size() && words.size() <= 32, "invalid fixture unit");
    return {kind, *words.begin(), std::vector<u32>(words)};
}
static u32 ppu_x(u32 xo, u32 rt = 4, u32 ra = 5, u32 rb = 6, bool rc = false)
{
    ppu_opcode_t op{31u << 26 | xo << 1}; op.rd = rt; op.ra = ra; op.rb = rb; op.rc = rc; return op.opcode;
}
static u32 spu_rr(u32 code, u32 rt = 4, u32 ra = 5, u32 rb = 6)
{
    spu_opcode_t op{code << 21}; op.rt = rt; op.ra = ra; op.rb = rb; return op.opcode;
}
static std::vector<aot::instruction> cases()
{
    using namespace ppu_instructions;
    std::vector<aot::instruction> result{
        unit(1, {ADDI(4, 5, -37), ORI(6, 4, 0x8123), XORIS(7, 6, 0x1234)}),
        unit(1, {ppu_x(266), ppu_x(40, 7, 4, 6, true)}), // add, subf.
        unit(1, {ppu_x(10, 4, 5, 6, true), ppu_x(138, 7, 4, 6, true)}), // addc., adde.
        unit(1, {ppu_x(28), ppu_x(316, 7, 4, 6, true)}), // and, xor.
        unit(1, {LD(4, 3, 32), STW(4, 3, 80), LWZ(5, 3, 80)}),
        unit(1, {STW(4, 3, 4), LWZ(5, 3, 4), ADDI(6, 5, -1)}),
        unit(1, {ADDI(4, 5, 17), B(16, false, true)}),
        unit(1, {ADDI(4, 5, 17), B(0x20000, true, true)}),
        unit(1, {BC(12, 0, 16, false, false)}),
        unit(1, {BC(16, 0, -4, false, true)}),
        unit(1, {BCLR(20, 0, 0, false)}),
        unit(1, {BCCTR(20, 0, 0, true)}),
        unit(1, {0xfc85302au}), // fadd f4,f5,f6
        unit(1, {0xec85302au}), // fadds f4,f5,f6
        unit(2, {spu_instructions::AI(4, 5, -37), spu_instructions::XOR(7, 4, 6)}),
        unit(2, {spu_instructions::SF(4, 5, 6), spu_instructions::CGTI(7, 4, -3)}),
        unit(2, {spu_rr(0xc0), spu_rr(0x41, 7, 4, 6)}),
        unit(2, {spu_rr(0x240), spu_rr(0x2c0, 7, 4, 6)}), // signed compare, equal
        unit(2, {spu_instructions::LQD(4, 3, 32), spu_instructions::STQD(4, 3, 64)}),
        unit(2, {spu_instructions::STQD(4, 3, 16), spu_instructions::LQD(5, 3, 16)}),
        unit(2, {spu_instructions::AI(4, 5, 3), spu_instructions::BR(32)}),
        unit(2, {spu_instructions::BRNZ(4, 32)}),
        unit(2, {spu_instructions::BRNZ(4, -16)}),
        unit(2, {spu_rr(0xf, 4, SPU_RdInMbox, 0)}), // rchcnt: native runtime helper
        unit(2, {spu_rr(0xf, 11, SPU_RdInMbox, 0), spu_instructions::AI(5, 11, 7)}), // completed channel count continues in native CFG
        unit(2, {spu_rr(0xd, 4, SPU_RdInMbox, 0)}),
        unit(2, {spu_rr(0xd, 4, SPU_RdEventMask, 0)}),
        unit(2, {spu_rr(0xf, 4, SPU_RdEventStat, 0)}),
        unit(2, {spu_instructions::WRCH(SPU_WrEventMask, 4)}),
        unit(2, {spu_instructions::WRCH(MFC_LSA, 4)}),
        unit(2, {spu_instructions::WRCH(MFC_LSA, 4), spu_instructions::WRCH(MFC_EAL, 5),
            spu_instructions::WRCH(MFC_Size, 6), spu_instructions::WRCH(MFC_TagID, 7),
            spu_rr(0xd, 8, MFC_RdTagMask, 0), spu_instructions::AI(9, 8, 7)}), // native DMA setup stays compiled
        unit(2, {spu_rr(0x2c4)}), // fa
        unit(2, {spu_rr(0x2c2)}), // fcgt: native ARM64 vector-select workaround
        unit(2, {spu_instructions::ILA(4, 71), (0x7fu << 24) | (72u << 14) | (4u << 7)}), // heqi false: PC comes from context
        unit(2, {0x1c000000, 0x35080000}), // captured AI/BI.d: native atomic interrupt-token store
        unit(1, {ADDI(11, 0, 42), SC(0)}), // syscall number must cross the AOT C boundary
    };
    return result;
}
static void random_ppu(ppu_thread& cpu, std::mt19937_64& random, u32 memory, u32 pc)
{
    cpu.state.store(bs_t<cpu_flag>{});
    cpu.cia = pc; cpu.lr = pc + 0x1000; cpu.ctr = pc + 0x2000;
    cpu.xer = {}; cpu.xer.ca = random() & 1; cpu.xer.ov = random() & 1; cpu.xer.so = random() & 1;
    cpu.sat = {}; cpu.nj = false; cpu.fpscr.bits = {};
    for (auto& value : cpu.gpr) value = random();
    cpu.gpr[3] = memory;
    for (auto& value : cpu.fpr) value = double(s64(random() % 200000) - 100000) / 128.0;
    for (auto& value : cpu.vr) value = v128::from64(random(), random());
    for (u32 i = 0; i < 32; ++i) cpu.cr[i] = (random() & 1) != 0;
}
static void copy_ppu(ppu_thread& destination, const ppu_thread& source)
{
    destination.state.store(bs_t<cpu_flag>{}); destination.cia = source.cia; destination.lr = source.lr; destination.ctr = source.ctr;
    std::memcpy(destination.gpr, source.gpr, sizeof(source.gpr));
    std::memcpy(destination.fpr, source.fpr, sizeof(source.fpr));
    std::memcpy(destination.vr, source.vr, sizeof(source.vr));
    destination.cr = source.cr; destination.xer = source.xer; destination.sat = source.sat; destination.nj = source.nj;
    destination.fpscr.bits = source.fpscr.bits;
}
static void compare_ppu(const ppu_thread& reference, const ppu_thread& compiled, usz index)
{
    const auto message = "PPU unit " + std::to_string(index);
    check(!std::memcmp(reference.gpr, compiled.gpr, sizeof(reference.gpr)), message + " GPR mismatch");
    check(!std::memcmp(reference.fpr, compiled.fpr, sizeof(reference.fpr)), message + " FPR mismatch");
    check(!std::memcmp(reference.vr, compiled.vr, sizeof(reference.vr)), message + " VR mismatch");
    check(!std::memcmp(&reference.cr, &compiled.cr, sizeof(reference.cr)), message + " CR mismatch");
    check(!std::memcmp(&reference.xer, &compiled.xer, sizeof(reference.xer)), message + " XER mismatch");
    check(!std::memcmp(&reference.fpscr.bits, &compiled.fpscr.bits, sizeof(reference.fpscr.bits)), message + " FPSCR mismatch");
    check(!std::memcmp(&reference.sat, &compiled.sat, sizeof(reference.sat)) && reference.nj == compiled.nj, message + " VSCR mismatch");
    check(reference.lr == compiled.lr && reference.ctr == compiled.ctr && reference.cia == compiled.cia, message + " control state mismatch");
}
static void test_preparation_workers()
{
 const auto worker_check = [](bool value) { check(value, "native compiler worker invariant"); };

 std::array<std::atomic<int>, 32> visits{};
 std::atomic<int> active{0}, peak{0}, ready{0};
 aot::preparation_tasks(visits.size(), 100, [&](std::size_t index) {
   worker_check(thread_ctrl::get_current() != nullptr);
#ifdef __APPLE__
   worker_check(pthread_get_stacksize_np(pthread_self()) >= 0x800000);
#endif
   const auto now = ++active;
   auto old = peak.load(); while (now > old && !peak.compare_exchange_weak(old, now)) {}
   if (index < 3) {
     ++ready;
     const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
     while (ready != 3 && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
     worker_check(ready == 3);
   }
   ++visits[index]; --active;
 });
 worker_check(active == 0 && peak == 3);
 for (auto& count : visits) worker_check(count == 1);
 int serial = 0;
 aot::preparation_tasks(7, 0, [&](std::size_t index) { worker_check(index == static_cast<std::size_t>(serial++)); });
 worker_check(serial == 7);
 aot::preparation_tasks(0, 2, [&](std::size_t) { worker_check(false); });
 const auto caller = std::this_thread::get_id();
 aot::preparation_tasks(1, 2, [&](std::size_t) { worker_check(std::this_thread::get_id() != caller); });
 bool caught = false;
 try {
   aot::preparation_tasks(100, 2, [&](std::size_t) {
     struct guard { std::atomic<int>& n; guard(std::atomic<int>& x):n(x){++n;} ~guard(){--n;} } alive(active);
     throw 42;
   });
 } catch (int code) { worker_check(code == 42); caught = true; }
 worker_check(caught && active == 0);
 std::atomic<bool> cancelled{false}; std::atomic<int> claimed{0}; caught = false;
 try {
   aot::preparation_tasks(100, 2, [&](std::size_t) {
     ++claimed; cancelled = true;
     if (cancelled) throw std::runtime_error("cancelled");
   });
 } catch (const std::runtime_error&) { caught = true; }
 worker_check(caught && claimed <= 2);
 // The first signer waits until both compiler workers move on. This would
 // deadlock if linking/signing still occupied a compiler worker.
 std::array<std::atomic<int>, 24> produced{}, consumed{};
 std::atomic<int> produced_count{0}, signing{0};
 aot::preparation_pipeline(produced.size(), 2, [&](std::size_t index) {
   worker_check(thread_ctrl::get_current() != nullptr);
   ++produced[index]; ++produced_count;
 }, [&](std::size_t index) {
   worker_check(thread_ctrl::get_current() != nullptr && ++signing == 1);
   const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
   while (produced_count < 4 && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
   worker_check(produced_count >= 4 && produced[index] == 1);
   ++consumed[index]; --signing;
 });
 for (std::size_t i = 0; i < produced.size(); ++i) worker_check(produced[i] == 1 && consumed[i] == 1);
 worker_check(signing == 0);
 for (bool producer_failure : {false, true}) {
   caught = false; active = 0;
   try {
     aot::preparation_pipeline(100, 2, [&](std::size_t) {
       struct guard { std::atomic<int>& n; guard(std::atomic<int>& x):n(x){++n;} ~guard(){--n;} } alive(active);
       if (producer_failure) throw 73;
     }, [&](std::size_t index) { if (index < 100) throw 74; });
   } catch (int code) { worker_check(code == (producer_failure ? 73 : 74)); caught = true; }
   worker_check(caught && active == 0);
 }
 aot::preparation_pipeline(0, 2, [&](std::size_t) { worker_check(false); }, [&](std::size_t) { worker_check(false); });
 std::cout << "Bounded native preparation pipeline: overlap, exactly-once consumption, joined producer/signer failure passed\n";
 std::cout << "Native compiler workers: 8 MiB stacks, bounded overlap, exactly once and joined cancellation/errors passed\n";
 for (const std::size_t size : {1, 2, 511, 512}) {
   std::vector<unsigned> output(size); unsigned declines = 0;
   const auto emit = [&](std::size_t first, std::size_t count, bool descriptors_only) {
     if (descriptors_only) { worker_check(first == size / 2 && count == 1); ++declines; ++output[first]; return true; }
     if (count > 17 || (first <= size / 2 && size / 2 < first + count)) return false;
     for (auto i = first; i < first + count; ++i) ++output[i];
     return true;
   };
   aot::prepare_partition(0, size, emit);
   worker_check(declines == 1 && std::all_of(output.begin(), output.end(), [](unsigned count) { return count == 1; }));
 }
 const auto cancelled_emit = [](std::size_t first, std::size_t, bool) -> bool { if (first == 0) throw 42; return false; };
 caught = false; try { aot::prepare_partition(0, 512, cancelled_emit); } catch (int code) { caught = code == 42; }
 worker_check(caught);
 std::cout << "Native AOT size recovery: recursive splitting, exact unit coverage, single-unit interpreter decline and cancellation passed\n";
}

int main(int argc, char** argv) try
{
    check((argc == 5 && std::string_view(argv[2]) == "--game-inventory") || argc == 2 || (argc == 3 && (std::string(argv[2]) == "--native-abi-proof" || std::string(argv[2]) == "--analysis-only" || std::string(argv[2]) == "--native-only")) || (argc == 4 && (std::string(argv[2]) == "--native-abi-jit" || std::string(argv[2]) == "--native-abi-load" || std::string(argv[2]) == "--inventory" || std::string(argv[2]) == "--inventory-summary" || std::string(argv[2]) == "--game-analysis" || std::string(argv[2]) == "--load-existing")), "pass an isolated test output directory");
    const bool native_jit_proof = argc == 4 && std::string_view(argv[2]) == "--native-abi-jit";
    check(native_jit_proof ? !rpcs3::ios::jit::is_jitless() : rpcs3::ios::jit::is_aot() && rpcs3::ios::jit::is_jitless(), "launch with AOT/JIT-less environment before constructors");
    const auto root = path::absolute(argv[1]);
    path::create_directories(root / "Data"); path::create_directories(root / "Cache");
    const auto support = (root / "Data").string(), cache = (root / "Cache").string();
    rpcs3_ios_config config{30, sizeof(config), support.c_str(), cache.c_str(),
        +[](void*, int32_t level, const char* message) { if (level <= 5) std::cerr << "[" << level << "] " << message << '\n'; }, nullptr, nullptr, 0, 0};
    check(rpcs3_ios_initialize(&config) == RPCS3_IOS_OK, rpcs3_ios_last_error());
    const int fault_signals[] = {SIGBUS, SIGSEGV, SIGTRAP};
    struct sigaction original_actions[std::size(fault_signals)]{};
    for (usz i = 0; i < std::size(fault_signals); ++i)
        check(sigaction(fault_signals[i], nullptr, &original_actions[i]) == 0, "observe native fault handler");
    const auto check_fault_handlers = [&]
    {
        for (usz i = 0; i < std::size(fault_signals); ++i)
        {
            struct sigaction current{};
            check(sigaction(fault_signals[i], nullptr, &current) == 0, "observe post-link fault handler");
            const auto& original = original_actions[i];
            const bool same = original.sa_flags & SA_SIGINFO ? current.sa_sigaction == original.sa_sigaction :
                current.sa_handler == original.sa_handler;
            check(current.sa_flags == original.sa_flags && same,
                "AOT compilation/linking replaced native signal handler " + std::to_string(fault_signals[i]));
            for (int signal = 1; signal < NSIG; ++signal)
                check(sigismember(&current.sa_mask, signal) == sigismember(&original.sa_mask, signal), "AOT linker changed native signal mask");
        }
    };
    std::cerr << "Core initialized; setting native interpreter modes\n";
    g_cfg.core.ppu_decoder.set(ppu_decoder_type::_static); g_cfg.core.spu_decoder.set(spu_decoder_type::_static);
    g_cfg.core.ppu_debug.set(false);
    for (const auto count : {AudioChannelCnt::STEREO, AudioChannelCnt::SURROUND_5_1, AudioChannelCnt::SURROUND_7_1})
    {
        NullAudioBackend backend;
        check(backend.Open("", AudioFreq::FREQ_44K, AudioSampleSize::S16, count, audio_channel_layout::automatic), "open Null audio");
        check(backend.get_channels() == u32(count) && backend.get_channel_layout() != audio_channel_layout::automatic &&
            backend.get_sampling_rate() == 44100 && backend.get_sample_size() == 2, "Null audio metadata contract");
        backend.Close();
    }
    std::cout << "Native Null audio: negotiated rate/sample/channel/layout metadata passed\n";
    {
        cfg_root effective;
        effective.core.ppu_use_nj_bit.set(true);
        effective.core.ppu_set_vnan.set(true);
        effective.core.ppu_set_fpcc.set(true);
        const auto saved = effective.to_string();
        normalize_ppu_llvm_settings(effective);
        check(!effective.core.ppu_use_nj_bit && !effective.core.ppu_set_vnan && !effective.core.ppu_set_fpcc, "LLVM numerical normalization");
        cfg_root persisted; persisted.from_string(saved);
        check(persisted.core.ppu_use_nj_bit && persisted.core.ppu_set_vnan && persisted.core.ppu_set_fpcc, "normalization altered saved configuration");
        std::cout << "AOT numerical contract: effective LLVM options normalized, saved settings preserved\n";
    }
    vm::init();
    struct vm_cleanup { ~vm_cleanup() { g_fxo->clear(); vm::close(); } } cleanup;
    if (argc == 4 && (native_jit_proof || std::string_view(argv[2]) == "--native-abi-load")) { test_aot_native_abi_load(root.string(), argv[3], native_jit_proof); return 0; }
    if (argc == 3 && std::string_view(argv[2]) == "--native-abi-proof") { test_aot_native_abi(root.string()); return 0; }
    if (argc == 5) { aot::test_native_aot_game_inventory(argv[3], argv[4]); return 0; }
    if (argc != 4 && !(argc == 3 && std::string_view(argv[2]) == "--native-only"))
    {
        test_aot_native_analysis();
        aot::test_native_aot_inventory_history(root.string());
        std::cout << "Native AOT history: cumulative PPU/SPU discoveries, deduplication and incompatible inventory rejection passed\n";
        std::cout << "Native AOT observations: core-update recompilation, exact executable rejection, source/settings/patch identity and current branch boundaries passed\n";
        aot::test_native_aot_digest_table(root.string());
        std::cout << "Native AOT digest tables: 64/65/1126/2048 modules and damaged/oversized rejection passed\n";
        if (argc == 3) return 0;
    }
    if (argc == 4 && std::string(argv[2]) == "--game-analysis") { test_aot_native_game_analysis(argv[3]); return 0; }
    if (argc == 4 && std::string(argv[2]) == "--inventory-summary")
    {
        const auto entries = aot::test_native_aot_inventory(argv[3]);
        struct summary { u64 entries = 0, words = 0; u32 maximum_words = 0; };
        std::array<summary, 4> totals{};
        std::set<aot::instruction> identities;
        for (auto entry : entries)
        {
            const bool native = entry.address || !entry.blocks.empty();
            auto& total = totals[(entry.guest_kind - 1) * 2 + unsigned(native)];
            const auto words = std::max<usz>(entry.words.size(), 1);
            ++total.entries; total.words += words; total.maximum_words = std::max(total.maximum_words, ::narrow<u32>(words));
            entry.entry_offset = 0;
            if (!entry.words.empty()) entry.opcode = entry.words.front();
            identities.insert(std::move(entry));
        }
        std::ofstream report(root / "inventory-summary.json");
        report << "{\"entries\":" << entries.size() << ",\"inventory_sha256\":\"" << aot::test_native_aot_inventory_digest(entries)
            << "\",\"identities_ignoring_entry_offset\":" << identities.size() << ",\"groups\":[";
        for (usz i = 0; i < totals.size(); ++i)
        {
            const auto& total = totals[i];
            if (i) report << ',';
            report << "{\"kind\":" << i / 2 + 1 << ",\"native_metadata\":" << (i % 2 ? "true" : "false")
                << ",\"entries\":" << total.entries << ",\"words\":" << total.words << ",\"maximum_words\":" << total.maximum_words << '}';
        }
        report << "]}\n";
        std::cout << "Inventory summary uses native manifest validation; counts are not execution coverage\n";
        return 0;
    }
    if (argc == 4 && std::string(argv[2]) == "--inventory")
    {
        auto observed = aot::test_native_aot_inventory(argv[3]);
        std::erase_if(observed, [](const auto& item) { return item.guest_kind != 2; });
        auto options = aot::native_object_options();
#ifndef RPCS3_IOS_AOT_SIMULATOR
    options.platform = aot::object_platform::host_macos;
#endif
        u32 translated = 0; std::map<std::string, u32> declined;
        for (usz offset = 0; offset < observed.size(); offset += 128)
        {
            std::vector<char> object; std::string error; aot::object_statistics stats;
            const auto count = std::min<usz>(128, observed.size() - offset);
            std::cerr << "Captured SPU units " << offset << "/" << observed.size() << std::endl;
            check(aot::emit_instructions(std::span(observed).subspan(offset, count), options, object, error, &stats), error);
            const auto object_path = (root / ("captured-" + std::to_string(offset) + ".o")).string();
            const auto library_path = (root / ("captured-" + std::to_string(offset) + ".dylib")).string();
            std::ofstream(object_path, std::ios::binary).write(object.data(), object.size());
            const std::string objects[] = {object_path};
            check(aot::link_module(objects, library_path, "@rpath/rpcs3-aot-captured-" + std::to_string(offset) + ".dylib", options.platform, error), error);
            translated += stats.spu;
            for (const auto& [reason, count] : stats.declined) declined[reason] += count;
        }
        for (const auto& [reason, count] : declined) std::cout << "Declined " << count << ": " << reason << std::endl;
        std::cout << "Captured SPU native object emission/linking: " << translated << "/" << observed.size() << std::endl;
        return 0;
    }
    test_preparation_workers();
    test_aot_native_spu_cache(root.string());
    vm::reserve_map(vm::main, 0x10000, 0x1fff0000, vm::page_64k_size);
    const u32 code = vm::alloc(0x10000, vm::main);
    check(code != 0, "allocate guest test data");
    {
        const auto hook_code = code + 0x4000;
        // Actual native registration, with unchanged guest instruction bytes.
        vm::write32(hook_code, ppu_instructions::ADDI(4, 5, 1));
        vm::write32(hook_code + 4, ppu_instructions::BLR());
        check(!ppu_aot_code_eligible(hook_code, 2), "unregistered readable data admitted");
        ppu_register_range(code, 0x10000);
        check(ppu_aot_code_eligible(hook_code, 2), "normal registered code rejected");
        check(ppu_form_branch_to_code(hook_code + 4, hook_code + 16, false, false), "register internal far jump");
        check(!ppu_aot_code_eligible(hook_code, 2), "internal far jump bypassed");
        check(vm::read32(hook_code + 4) == ppu_instructions::BLR(), "hook modified guest bytes");
        ppu_register_range(code, 0x10000);
        check(ppu_aot_code_eligible(hook_code, 2), "re-registered code remained rejected");
        check(ppu_breakpoint(hook_code, true), "install native breakpoint");
        check(!ppu_aot_code_eligible(hook_code, 2), "breakpoint bypassed");
        check(ppu_breakpoint(hook_code, false), "remove native breakpoint");
        check(ppu_aot_code_eligible(hook_code, 2), "removed breakpoint remained rejected");
        ppu_register_function_at(hook_code, 4, +[](ppu_thread&, ppu_opcode_t, be_t<u32>*, ppu_intrp_func*) {});
        check(!ppu_aot_code_eligible(hook_code, 2), "registered replacement bypassed");
        ppu_register_range(code, 0x10000);
        check(ppu_aot_code_eligible(hook_code, 2), "restored registration rejected");
        std::cout << "AOT PPU eligibility: native far jumps, breakpoints, replacements and unchanged guest words passed\n";
    }
    std::cerr << "Guest VM ready; constructing native CPU contexts\n";
    id_manager::g_id = ppu_thread::id_base;
    auto reference_ppu = std::make_unique<ppu_thread>(ppu_thread_params{}, "AOT reference", 1000, -1);
    id_manager::g_id = ppu_thread::id_base + 1;
    auto compiled_ppu = std::make_unique<ppu_thread>(ppu_thread_params{}, "AOT compiled", 1000, -1);
    id_manager::g_id = spu_thread::id_base;
    auto reference_spu = std::make_unique<spu_thread>(nullptr, 0, "AOT reference", 0);
    id_manager::g_id = spu_thread::id_base + 1;
    auto compiled_spu = std::make_unique<spu_thread>(nullptr, 0, "AOT compiled", 1);
    spu_thread::map_ls(*reference_spu->shm, reference_spu->ls);
    spu_thread::map_ls(*compiled_spu->shm, compiled_spu->ls);
    ppu_interpreter_rt ppu_interpreter;
    spu_interpreter_rt spu_interpreter;
    std::cerr << "Native CPU contexts ready\n";
    // A successful channel read is not a safe interrupt boundary until the
    // generated caller has stored its result and committed the next guest PC.
    // The native interpreter permits pending CPU work at the outer loop only.
    {
        auto& cpu = *compiled_spu;
        cpu.state.store(cpu_flag::pending);
        cpu.pc = 0x100;
        cpu.allow_interrupts_in_cpu_work = true;
        cpu.interrupt_requires_escape = true;
        cpu.interrupts_enabled = true;
        cpu.cpu_work_iteration_count = 0;
        spu_thread::ch_events_t events{}; events.mask = SPU_EVENT_S1; events.events = SPU_EVENT_S1; events.count = true;
        cpu.ch_events.store(events);
        const auto read = reinterpret_cast<u32 (*)(spu_thread*, u32)>(aot::resolve_runtime_symbol("spu_read_channel"));
        check(read != nullptr, "resolve file channel read helper");
        bool returned = false;
        u32 value = 0;
        rpcs3::ios::run_static_interpreter([&] { value = read(&cpu, SPU_RdEventMask); returned = true; });
        check(returned && value == SPU_EVENT_S1 && cpu.pc == 0x100 && cpu.interrupts_enabled,
            "file channel read serviced pending work before caller committed result/PC");
        const auto write = reinterpret_cast<void (*)(spu_thread*, u32, u32)>(aot::resolve_runtime_symbol("spu_write_channel"));
        check(write != nullptr, "resolve file channel write helper");
        returned = false;
        rpcs3::ios::run_static_interpreter([&] { write(&cpu, MFC_LSA, 0x1230); returned = true; });
        check(returned && cpu.ch_mfc_cmd.lsa == 0x1230 && cpu.pc == 0x100 && cpu.interrupts_enabled,
            "file channel write serviced pending work before caller committed PC");
        cpu.state.store(bs_t<cpu_flag>{}); cpu.ch_events.store({});
        cpu.allow_interrupts_in_cpu_work = false;
        std::cout << "File SPU channel completion: pending interrupt deferred until caller commit\n";
    }
    // Exercise native multi-block program analysis as used by Mega settings,
    // without persisting a production setting or changing interpreter semantics.
    const auto original_block_size = g_cfg.core.spu_block_size.get();
    struct restore_block_size { spu_block_size_type value; ~restore_block_size() { g_cfg.core.spu_block_size.set(value); } } restore_blocks{original_block_size};
    g_cfg.core.spu_block_size.set(spu_block_size_type::mega);
    auto inputs = cases();
    const auto basic_count = inputs.size();
    aot::instruction ppu_function{1, ppu_instructions::ADDI(4, 0, 0), {ppu_instructions::ADDI(4, 0, 0)}};
    for (u32 i = 0; i < 64; ++i) ppu_function.words.push_back(ppu_instructions::ADDI(4, 4, 1));
    ppu_function.words.push_back(ppu_instructions::BC(16, 0, -256, false, false));
    ppu_function.words.push_back(ppu_instructions::BCLR(20, 0, 0, false));
    ppu_function.blocks = {{0, 1}, {1, 65}, {66, 1}};
    ppu_function.address = 0x10000;
    inputs.push_back(ppu_function);
    aot::instruction spu_program{2, spu_instructions::ILA(4, 0), {spu_instructions::ILA(4, 0)}};
    for (u32 i = 0; i < 64; ++i) spu_program.words.push_back(spu_instructions::AI(4, 4, 1));
    spu_program.words.push_back(spu_instructions::AI(5, 5, -1));
    spu_program.words.push_back(spu_instructions::BRNZ(5, -260));
    spu_program.words.push_back(0x35000000u);
    spu_program.address = 0x100;
    inputs.push_back(spu_program);
    auto offset_program = spu_program;
    offset_program.entry_offset = 1; offset_program.opcode = offset_program.words[1];
    inputs.push_back(offset_program);
    auto spu_sync = unit(2, {spu_instructions::STQD(4, 3, 0), 0x00400000u,
        spu_instructions::AI(5, 5, 1), 0x35000000u, spu_instructions::AI(6, 6, 1), 0x35000000u});
    spu_sync.address = 0x100;
    inputs.push_back(spu_sync);
    auto sparse = unit(2, {spu_instructions::BRNZ(5, 32), 0, 0, 0, 0, 0, 0, 0, spu_instructions::AI(4, 4, 1), 0x35000000u});
    sparse.address = 0x100; sparse.blocks = {{0, 1}, {8, 2}};
    inputs.push_back(sparse);
    auto zero_origin = spu_program;
    zero_origin.address = 0; zero_origin.blocks = {{0, u32(zero_origin.words.size())}};
    inputs.push_back(zero_origin);
    auto absolute_ppu = unit(1, {ppu_instructions::B(0x10008, true, false), ppu_instructions::ADDI(4, 0, 1),
        ppu_instructions::ADDI(4, 0, 42), ppu_instructions::BCLR(20, 0, 0, false)});
    absolute_ppu.address = 0x10000; absolute_ppu.blocks = {{0, 1}, {2, 2}};
    inputs.push_back(absolute_ppu);
    const auto ppu_resume_index = inputs.size();
    auto ppu_resume = unit(1, {ppu_instructions::ADDI(11, 0, 42), ppu_instructions::SC(0),
        ppu_instructions::ADDI(4, 3, 7), ppu_instructions::BLR()});
    ppu_resume.address = 0x10000; ppu_resume.blocks = {{0, 4}}; inputs.push_back(ppu_resume);
    const auto spu_resume_index = inputs.size();
    auto spu_resume = unit(2, {spu_rr(0xd, 4, SPU_RdInMbox, 0), spu_instructions::AI(5, 4, 7), 0x35000000});
    spu_resume.address = 0x100; spu_resume.blocks = {{0, 3}}; inputs.push_back(spu_resume);
    const auto spu_absolute_index = inputs.size();
    for (const bool link : {false, true})
    {
        spu_opcode_t branch{(link ? 0x62u : 0x60u) << 23}; branch.i16 = 0x108 / 4; branch.rt = link ? 5 : 0;
        auto absolute = unit(2, {branch.opcode, spu_instructions::AI(4, 4, 1), spu_instructions::AI(4, 4, 42), 0x35000000});
        absolute.address = 0x100; absolute.blocks = {{0, 1}, {2, 2}}; inputs.push_back(std::move(absolute));
    }
    auto options = aot::native_object_options();
#ifndef RPCS3_IOS_AOT_SIMULATOR
    options.platform = aot::object_platform::host_macos;
#endif
    std::string library;
    if (argc == 4 && std::string(argv[2]) == "--load-existing")
    {
        library = path::absolute(argv[3]).string();
        std::cout << "Loaded existing native AOT library without compilation\n";
    }
    else
    {
        std::cerr << "Emitting file-only LLVM objects\n";
        std::vector<char> object; std::string error; aot::object_statistics compiled{};
        check(aot::emit_instructions(inputs, options, object, error, &compiled), error);
        check(compiled.ppu && compiled.spu, "both native translators must produce signed-module entries");
        library = (root / "native.dylib").string();
        aot::preparation_pipeline(2, 2, [&](std::size_t index)
        {
            std::vector<char> parallel;
            std::string failure;
            aot::object_statistics stats;
            check(aot::emit_instructions(inputs, options, parallel, failure, &stats), failure);
            check(parallel == object && stats.ppu == compiled.ppu && stats.spu == compiled.spu &&
                stats.declined == compiled.declined, "parallel LLVM output differs from serial output");
            const auto stem = index ? "parallel-native" : "native";
            const auto obj = (root / (std::string(stem) + ".o")).string();
            std::ofstream(obj, std::ios::binary).write(parallel.data(), parallel.size());
        }, [&](std::size_t index)
        {
            const auto stem = index ? "parallel-native" : "native";
            const auto obj = (root / (std::string(stem) + ".o")).string();
            const auto dylib = (root / (std::string(stem) + ".dylib")).string();
            std::string failure;
            const std::string objects[] = {obj};
            check(aot::link_module(objects, dylib, "@rpath/rpcs3-aot-native-test.dylib", options.platform, failure), failure);
        });
        std::cout << "Parallel native AOT: independent LLVM contexts produce identical objects and serialized linking passed\n";
        check_fault_handlers();
        const auto bad = (root / "invalid-link.o").string();
        const auto rejected = (root / "invalid-link.dylib").string();
        std::ofstream(bad, std::ios::binary) << "not a Mach-O object";
        const std::string invalid_objects[] = {bad};
        check(!aot::link_module(invalid_objects, rejected, "@rpath/rpcs3-aot-invalid.dylib", options.platform, error) &&
            !path::exists(rejected), "invalid object link must fail without publishing");
        check_fault_handlers();
        const std::string valid_objects[] = {(root / "native.o").string()};
        check(aot::link_module(valid_objects, (root / "after-failed-link.dylib").string(),
            "@rpath/rpcs3-aot-after-failure.dylib", options.platform, error), "reuse linker after ordinary input error: " + error);
        std::cout << "Native linker: failed input leaves no output and permits a subsequent valid link\n";
        auto fallback_options = options; fallback_options.descriptors_only = true;
        aot::object_statistics fallback_stats; std::vector<char> fallback_object;
        check(aot::emit_instructions(inputs, fallback_options, fallback_object, error, &fallback_stats), error);
        check(fallback_stats.ppu == 0 && fallback_stats.spu == 0 &&
            fallback_stats.declined["Native unit exceeds signed module size limit"] == inputs.size(), "resource decline emitted translated code");
        const auto fallback_path = (root / "descriptor-only.o").string();
        std::ofstream(fallback_path, std::ios::binary).write(fallback_object.data(), fallback_object.size());
        const std::string fallback_objects[] = {fallback_path};
        check(aot::link_module(fallback_objects, (root / "descriptor-only.dylib").string(), "@rpath/rpcs3-aot-descriptor-only.dylib", options.platform, error), error);
    }

    {
        const auto fallback = path::path(library).parent_path() / "descriptor-only.dylib";
        auto* handle = dlopen(fallback.c_str(), RTLD_NOW | RTLD_LOCAL);
        check(handle != nullptr, "load resource-decline descriptor image");
        const auto descriptor = reinterpret_cast<const rpcs3_aot_module_v2* (*)()>(dlsym(handle, "rpcs3_aot_module_v2"))();
        check(descriptor && descriptor->entry_count == inputs.size() && descriptor->import_count == 0, "resource-decline interface");
        for (u32 i = 0; i < descriptor->entry_count; ++i) check(descriptor->entries[i].invoke == nullptr, "resource decline contains executable CPU entry");
        std::cout << "Native resource decline: file-loaded descriptors preserve every unit with null invocation and no runtime imports\n";
    }
    aot::test_native_aot_admission(root.string(), library, inputs);
    check_fault_handlers();
    std::cout << "AOT compilation/linking preserves native SIGBUS/SIGSEGV/SIGTRAP handlers and masks\n";
    auto handle = dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
    check(handle, handle ? "" : dlerror());
    const auto descriptor = reinterpret_cast<const rpcs3_aot_module_v2* (*)()>(dlsym(handle, "rpcs3_aot_module_v2"))();
    check(descriptor->entries[basic_count + 1].invoke == descriptor->entries[basic_count + 2].invoke &&
        descriptor->entries[basic_count + 1].invoke, "resumable SPU program entry duplicated native compilation");
    std::cout << "Native program reuse: distinct resumable descriptors share one verified translated body\n";
    const auto registration = reinterpret_cast<u32 (*)(const rpcs3_aot_runtime_context_v2*)>(dlsym(handle, "rpcs3_aot_register_runtime_v2"));
    check(descriptor && registration && descriptor->entry_count == inputs.size(), "native module descriptor");
    std::vector<const void*> imports;
    for (u32 i = 0; i < descriptor->import_count; ++i)
    {
        auto address = aot::resolve_runtime_symbol(descriptor->imports[i]);
        check(address, "unresolved native helper"); imports.push_back(reinterpret_cast<void*>(address));
    }
    check(!imports.empty(), "native runtime helper must be exercised");
    rpcs3_aot_runtime_context_v2 runtime{}; runtime.structure_size = sizeof(runtime); runtime.version = RPCS3_AOT_RUNTIME_VERSION;
    runtime.runtime_functions = imports.data(); runtime.runtime_function_count = imports.size();
    check(registration(&runtime) == 1, "register native helper table");
    --runtime.version; check(registration(&runtime) == 0, "reject stale runtime"); ++runtime.version;
    aot::select_entries(std::span(descriptor->entries, descriptor->entry_count));
    aot::restore_discoveries(inputs);
    check(aot::statistics().pending == 0, "prepared observations remained pending after generation activation");
    runtime.matches_code = [](const rpcs3_aot_runtime_context_v2* state, const u32* words, u32 count)
    {
        if (!count || count > 65536) return false;
        if (state->local_store ? (state->guest_pc >= SPU_LS_SIZE || count > (SPU_LS_SIZE - state->guest_pc) / 4)
            : !vm::check_addr(state->guest_pc, vm::page_readable, count * 4)) return false;
        const auto base = state->local_store ? static_cast<const u8*>(state->local_store) : static_cast<const u8*>(state->memory_base);
        return aot::matches_guest_words(base + state->guest_pc, words, count, state->verification_flags & RPCS3_AOT_SPARSE_SPU);
    };
    runtime.read_code = [](const rpcs3_aot_runtime_context_v2* state, u32* words, u32 capacity)
    {
        const auto base = state->local_store ? static_cast<const u8*>(state->local_store) : static_cast<const u8*>(state->memory_base);
        const auto code = reinterpret_cast<const be_t<u32>*>(base + state->guest_pc);
        for (u32 i = 0; i < capacity; ++i) words[i] = +code[i];
        return capacity;
    };
    {
        const auto& first = inputs.front();
        for (usz i = 0; i < first.words.size(); ++i) vm::write32(code + i * 4, first.words[i]);
        std::vector<u32> origins(descriptor->entry_count, UINT32_MAX); origins[0] = code;
        aot::select_entries(std::span(descriptor->entries, descriptor->entry_count), origins);
        check(aot::has_prepared_address(1, code) && aot::prepared_addresses(1, code, 4) == std::vector<u32>{code}, "native prepared PC registration");
        ppu_register_aot_range(code, 12);
        const auto registered = *reinterpret_cast<const ppu_intrp_func_t*>(vm::g_exec_addr + u64(code) * 2);
        check(ppu_aot_code_eligible(code, 3), "prebuilt native AOT bridge rejected");
        runtime.cpu = &*compiled_ppu; runtime.ppu_gpr = compiled_ppu->gpr;
        runtime.memory_base = vm::g_base_addr; runtime.guest_pc = code;
        const auto old_state = compiled_ppu->state.exchange({});
        std::array<u64, 32> before{}, expected{};
        std::copy_n(compiled_ppu->gpr, 32, before.begin());
        check(aot::execute(1, first.opcode, runtime), "direct bridge reference execution");
        std::copy_n(compiled_ppu->gpr, 32, expected.begin());
        const auto next_pc = runtime.next_pc;
        std::copy(before.begin(), before.end(), compiled_ppu->gpr);
        compiled_ppu->aot_runtime_context = &runtime;
        registered(*compiled_ppu, {first.opcode}, reinterpret_cast<be_t<u32>*>(vm::g_base_addr + code), nullptr);
        compiled_ppu->aot_runtime_context = nullptr;
        check(std::equal(expected.begin(), expected.end(), compiled_ppu->gpr) && compiled_ppu->cia == next_pc,
            "native registered bridge disagreed with direct signed execution");
        compiled_ppu->state.store(old_state);
        check(ppu_breakpoint(code, true) && !ppu_aot_code_eligible(code, 3), "breakpoint over native AOT bridge rejected/bypassed");
        check(ppu_breakpoint(code, false) && ppu_aot_code_eligible(code, 3), "AOT bridge breakpoint removal");
        check(*reinterpret_cast<const ppu_intrp_func_t*>(vm::g_exec_addr + u64(code) * 2) == registered, "native bridge not restored");
        const ppu_intrp_func_t replacement = +[](ppu_thread&, ppu_opcode_t, be_t<u32>*, ppu_intrp_func*) {};
        ppu_register_function_at(code, 4, replacement); ppu_register_aot_range(code, 12);
        check(*reinterpret_cast<const ppu_intrp_func_t*>(vm::g_exec_addr + u64(code) * 2) == replacement, "AOT registration replaced a native hook");
        ppu_register_range(code, 0x10000);
        aot::select_entries(std::span(descriptor->entries, descriptor->entry_count));
        std::cout << "Native PPU AOT data gates: eligible PCs, breakpoint restoration and native hook ownership passed\n";
    }
    for (u32 generation = 0; generation < 256; ++generation)
    {
        aot::select_entries({});
        check(aot::statistics().retained_dispatch_tables == 1, "empty dispatch tables retained across Stop");
        aot::select_entries(std::span(descriptor->entries, descriptor->entry_count));
        check(aot::statistics().retained_dispatch_tables == 1, "old generation metadata retained");
    }
    std::cout << "AOT lookup lifetime: 256 stopped-generation replacements retain one table\n";
    std::mt19937_64 random(0xa07a07);
    u64 comparisons = 0;
    for (usz index = 0; index + 1 < basic_count; ++index)
    {
        const auto& input = inputs[index]; const auto& entry = descriptor->entries[index];
        check(entry.invoke, "native unit declined: " + std::to_string(index));
        for (unsigned sample = 0; sample < 128; ++sample)
        {
            if (input.guest_kind == 1)
            {
                for (usz i = 0; i < input.words.size(); ++i) vm::write32(code + i * 4, input.words[i]);
                std::array<u8, 256> initial{}, expected{};
                for (auto& byte : initial) byte = random();
                std::memcpy(vm::base(code + 0x8000), initial.data(), initial.size());
                random_ppu(*reference_ppu, random, code + 0x8000, code); copy_ppu(*compiled_ppu, *reference_ppu);
                for (usz i = 0; i < input.words.size(); ++i)
                {
                    reference_ppu->cia = code + i * 4;
                    ppu_intrp_func end{+[](ppu_thread& cpu, ppu_opcode_t, be_t<u32>* next, ppu_intrp_func*) { cpu.cia = vm::get_addr(next); }};
                    ppu_interpreter.decode(input.words[i])(*reference_ppu, {input.words[i]}, vm::get_super_ptr<be_t<u32>>(code + i * 4), &end);
                }
                std::memcpy(expected.data(), vm::base(code + 0x8000), expected.size());
                std::memcpy(vm::base(code + 0x8000), initial.data(), initial.size());
                runtime.local_store = nullptr; runtime.cpu = compiled_ppu.get(); runtime.ppu_gpr = compiled_ppu->gpr; runtime.memory_base = vm::g_base_addr;
                runtime.guest_pc = code; runtime.next_pc = code + 4;
                check(aot::execute({1, input.opcode, {}}, runtime), "native PPU dispatch failed"); compiled_ppu->cia = runtime.next_pc;
                compare_ppu(*reference_ppu, *compiled_ppu, index);
                check(!std::memcmp(expected.data(), vm::base(code + 0x8000), expected.size()), "PPU memory mismatch");
            }
            else
            {
                // Match the native SPU worker's arithmetic environment.
                std::fesetround(FE_TOWARDZERO); gv_set_zeroing_denormals();
                reference_spu->state.store(bs_t<cpu_flag>{}); compiled_spu->state.store(bs_t<cpu_flag>{});
                reference_spu->pc = compiled_spu->pc = 0x100;
                reference_spu->interrupts_enabled = compiled_spu->interrupts_enabled = true;
                const auto interrupt_token = random() | 1;
                reference_spu->dec_intr_armed = compiled_spu->dec_intr_armed = interrupt_token;
                for (auto& reg : reference_spu->gpr) reg = v128::from64(random(), random());
                reference_spu->gpr[3] = v128::from32p(0x8000);
                if (input.opcode == spu_rr(0x2c4))
                    for (auto& reg : reference_spu->gpr) for (u32 lane = 0; lane < 4; ++lane) reg._f[lane] = float(s64(random() % 20000) - 10000) / 128.f;
                if (input.opcode == spu_rr(0x2c2))
                {
                    constexpr u32 boundaries[] = {0, 0x80000000, 1, 0x807fffff, 0x00800000, 0x7f7fffff, 0xff7fffff, 0x3f000000, 0xbf000000, 0x3f800000, 0xbf800000};
                    for (u32 lane = 0; lane < 4; ++lane)
                    {
                        reference_spu->gpr[5]._u32[lane] = boundaries[(sample + lane) % std::size(boundaries)];
                        reference_spu->gpr[6]._u32[lane] = boundaries[(sample / std::size(boundaries) + lane) % std::size(boundaries)];
                    }
                }
                if (input.opcode == spu_rr(0xf, 4, SPU_RdEventStat, 0))
                {
                    // With timer events masked there is no armed decrementer.
                    // Native RCHCNT need not refresh an unrelated timer token.
                    reference_spu->dec_intr_armed = compiled_spu->dec_intr_armed = 0;
                }
                if (input.opcode == spu_rr(0xf, 11, SPU_RdInMbox, 0))
                {
                    reference_spu->ch_in_mbox.clear(); compiled_spu->ch_in_mbox.clear();
                    for (u32 count = 0; count < sample % 4; ++count)
                    { reference_spu->ch_in_mbox.push(count); compiled_spu->ch_in_mbox.push(count); }
                }
                if (input.opcode == spu_rr(0xd, 4, SPU_RdInMbox, 0))
                {
                    reference_spu->ch_in_mbox.push(SPU_EVENT_S1);
                    compiled_spu->ch_in_mbox.push(SPU_EVENT_S1);
                }
                if (input.opcode == spu_instructions::WRCH(SPU_WrEventMask, 4))
                {
                    reference_spu->ch_events.store({}); compiled_spu->ch_events.store({});
                    reference_spu->gpr[4] = v128::from32p(SPU_EVENT_S1);
                }
                compiled_spu->gpr = reference_spu->gpr;
                for (usz i = 0; i < input.words.size(); ++i) reinterpret_cast<be_t<u32>*>(compiled_spu->ls + 0x100)[i] = input.words[i];
                std::array<u8, 256> initial{};
                for (auto& byte : initial) byte = random();
                std::memcpy(reference_spu->ls + 0x8000, initial.data(), initial.size());
                std::memcpy(compiled_spu->ls + 0x8000, initial.data(), initial.size());
                for (auto opcode : input.words)
                    if (spu_interpreter.decode(opcode)(*reference_spu, {opcode})) reference_spu->pc += 4;
                runtime.cpu = compiled_spu.get(); runtime.local_store = compiled_spu->ls; runtime.memory_base = vm::g_base_addr;
                runtime.spu_gpr_words = reinterpret_cast<u32*>(compiled_spu->gpr.data()); runtime.guest_pc = 0x100; runtime.next_pc = 0x104;
                check(aot::execute({2, input.opcode, {}}, runtime), "native SPU dispatch failed");
                if (input.opcode == spu_instructions::WRCH(MFC_LSA, 4) && input.words.size() > 1)
                {
                    for (u32 retry = 0; runtime.executed_words < input.words.size() && retry < 4096; ++retry)
                    {
                        runtime.guest_pc = 0x100;
                        check(aot::execute(2, input.opcode, runtime), "native SPU prefix refresh dispatch failed");
                    }
                    check(runtime.executed_words == input.words.size(), "cached short prefix permanently hid a matching native program");
                }
                compiled_spu->pc = runtime.next_pc;
                for (u32 reg = 0; reg < 128; ++reg)
                    for (u32 lane = 0; lane < 4; ++lane)
                        check(reference_spu->gpr[reg]._u32[lane] == compiled_spu->gpr[reg]._u32[lane],
                            fmt::format("SPU GPR mismatch unit=%u sample=%u r%u[%u]=%x/%x", index, sample, reg, lane,
                                reference_spu->gpr[reg]._u32[lane], compiled_spu->gpr[reg]._u32[lane]));
                check(reference_spu->pc == compiled_spu->pc, "SPU branch mismatch unit " + std::to_string(index));
                check(reference_spu->interrupts_enabled == compiled_spu->interrupts_enabled &&
                    reference_spu->dec_intr_armed.load() == compiled_spu->dec_intr_armed.load(), fmt::format("SPU interrupt state mismatch unit=%u sample=%u opcode=%x enabled=%u/%u token=%x/%x", index, sample, input.opcode, reference_spu->interrupts_enabled, compiled_spu->interrupts_enabled, reference_spu->dec_intr_armed.load(), compiled_spu->dec_intr_armed.load()));
                check(!std::memcmp(reference_spu->ls + 0x8000, compiled_spu->ls + 0x8000, initial.size()), "SPU LS mismatch");
                if (input.opcode == spu_instructions::WRCH(MFC_LSA, 4))
                {
                    check(reference_spu->ch_mfc_cmd.lsa == compiled_spu->ch_mfc_cmd.lsa, "SPU channel write mismatch");
                    if (input.words.size() > 1)
                        check(reference_spu->ch_mfc_cmd.eal == compiled_spu->ch_mfc_cmd.eal &&
                            reference_spu->ch_mfc_cmd.size == compiled_spu->ch_mfc_cmd.size &&
                            reference_spu->ch_mfc_cmd.tag == compiled_spu->ch_mfc_cmd.tag && runtime.executed_words == input.words.size(),
                            "SPU pure channel sequence did not commit native DMA setup in one invocation");
                }
                if (input.opcode == spu_rr(0xf, 11, SPU_RdInMbox, 0))
                { reference_spu->ch_in_mbox.clear(); compiled_spu->ch_in_mbox.clear(); }
            }
            ++comparisons;
        }
    }

    if (const char* profile = std::getenv("RPCS3_AOT_PROFILE"); profile && std::string_view(profile) == "1")
    {
        const auto stats = aot::statistics();
        check(stats.invocations_ppu > 0 && stats.invocations_spu > 0 && stats.code_checks > 0 && stats.code_words_checked >= stats.code_checks,
            "native diagnostic invocation/verification counters missing");
        std::cout << "AOT diagnostics: invocation and requested verification work counters passed\n";
    }

    // Whole native functions/programs must stay inside the compiled CFG across
    // branches, then yield with materialized state. Compare actual dynamic PCs.
    for (usz index = basic_count; index < basic_count + 3; ++index)
    {
        const auto& input = inputs[index];
        check(descriptor->entries[index].invoke, "native function/program declined " + std::to_string(index));
        for (u32 loops = 1; loops <= 9; ++loops)
        {
            if (input.guest_kind == 1)
            {
                for (usz i = 0; i < input.words.size(); ++i) vm::write32(code + i * 4, input.words[i]);
                random_ppu(*reference_ppu, random, code + 0x8000, code);
                reference_ppu->ctr = loops; reference_ppu->lr = code + 0x2000;
                copy_ppu(*compiled_ppu, *reference_ppu);
                u32 steps = 0;
                while (reference_ppu->cia >= code && reference_ppu->cia < code + input.words.size() * 4)
                {
                    check(++steps < 1000, "reference PPU loop did not terminate");
                    const auto pc = reference_ppu->cia; const auto opcode = vm::read32(pc);
                    ppu_intrp_func end{+[](ppu_thread& cpu, ppu_opcode_t, be_t<u32>* next, ppu_intrp_func*) { cpu.cia = vm::get_addr(next); }};
                    ppu_interpreter.decode(opcode)(*reference_ppu, {opcode}, vm::get_super_ptr<be_t<u32>>(pc), &end);
                }
                runtime.cpu = compiled_ppu.get(); runtime.memory_base = vm::g_base_addr; runtime.local_store = nullptr;
                runtime.ppu_gpr = compiled_ppu->gpr; runtime.guest_pc = code;
                check(runtime.matches_code(&runtime, descriptor->entries[index].words, descriptor->entries[index].word_count), "PPU full function exact code matcher");
                if (!aot::execute(1, input.opcode, runtime))
                {
                    const auto& e = descriptor->entries[index];
                    throw std::runtime_error(fmt::format("native PPU function dispatch: input=%x entry=%x/%x/%u/%u context=%u/%u expected=%u/%u", input.opcode,
                        e.guest_kind, e.opcode, e.word_count, e.entry_offset, runtime.structure_size, runtime.version, sizeof(runtime), RPCS3_AOT_RUNTIME_VERSION));
                }
                compiled_ppu->cia = runtime.next_pc;
                compare_ppu(*reference_ppu, *compiled_ppu, index);
                check(runtime.executed_words == steps && compiled_ppu->gpr[4] == loops * 64, "PPU native CFG dynamic count/result");
                compiled_ppu->state.store(cpu_flag::stop); runtime.remaining_words = 4096; runtime.executed_words = 0;
                descriptor->entries[index].invoke(&runtime);
                check(runtime.next_pc == code && runtime.executed_words == 0, "PPU pending stop did not yield before executing");
                compiled_ppu->state.store(bs_t<cpu_flag>{}); compiled_ppu->ctr = 3;
                runtime.remaining_words = 2; runtime.executed_words = 0;
                descriptor->entries[index].invoke(&runtime);
                check(runtime.next_pc == code + 4 && runtime.executed_words == 66 && compiled_ppu->ctr == 2,
                    "PPU native loop budget did not yield with materialized state");
                runtime.guest_pc = runtime.next_pc;
                check(aot::execute(1, input.words[1], runtime) && runtime.next_pc == code + 0x2000 && compiled_ppu->gpr[4] == 192,
                    "PPU yielded loop could not resume through the signed native entry table");
                runtime.guest_pc = code;
                vm::write32(code + 160, input.words[40] ^ 1);
                check(!aot::execute(1, input.opcode, runtime), "changed middle word of PPU function executed");
            }
            else
            {
                for (usz i = 0; i < input.words.size(); ++i) reinterpret_cast<be_t<u32>*>(compiled_spu->ls + 0x100)[i] = input.words[i];
                std::memcpy(reference_spu->ls, compiled_spu->ls, SPU_LS_SIZE);
                for (auto* cpu : {reference_spu.get(), compiled_spu.get()})
                {
                    cpu->state.store(bs_t<cpu_flag>{}); cpu->gpr.fill({});
                    cpu->gpr[0]._u32[3] = 0x2000; cpu->gpr[5] = v128::from32p(loops);
                    cpu->pc = 0x100 + input.entry_offset * 4;
                }
                u32 steps = 0;
                while (reference_spu->pc >= 0x100 && reference_spu->pc < 0x100 + input.words.size() * 4)
                {
                    check(++steps < 1000, "reference SPU loop did not terminate");
                    const auto opcode = +reinterpret_cast<const be_t<u32>*>(reference_spu->ls)[reference_spu->pc / 4];
                    if (spu_interpreter.decode(opcode)(*reference_spu, {opcode})) reference_spu->pc += 4;
                }
                runtime.cpu = compiled_spu.get(); runtime.local_store = compiled_spu->ls;
                runtime.spu_gpr_words = reinterpret_cast<u32*>(compiled_spu->gpr.data()); runtime.guest_pc = compiled_spu->pc;
                check(aot::execute(2, input.opcode, runtime), "native SPU program dispatch");
                check(runtime.next_pc == reference_spu->pc && !std::memcmp(compiled_spu->gpr.data(), reference_spu->gpr.data(), sizeof(reference_spu->gpr)),
                    "native SPU program CFG/SSA mismatch");
                check(runtime.executed_words == steps, "native SPU program dynamic count mismatch");
                compiled_spu->gpr[5] = v128::from32p(10000); compiled_spu->pc = runtime.guest_pc;
                runtime.remaining_words = 70; runtime.executed_words = 0;
                descriptor->entries[index].invoke(&runtime);
                check(runtime.executed_words >= 70 && runtime.executed_words < 200 && runtime.next_pc == 0x104,
                    "SPU native loop did not yield with a bounded work budget");
                runtime.guest_pc = runtime.next_pc;
                const auto before_resume = compiled_spu->gpr[5]._u32[3];
                check(aot::execute(2, input.words[1], runtime) && runtime.executed_words && compiled_spu->gpr[5]._u32[3] < before_resume,
                    "SPU yielded loop could not resume through the signed native chunk table");
                runtime.guest_pc = 0x100 + input.entry_offset * 4;
                compiled_spu->state.store(cpu_flag::stop); runtime.remaining_words = 4096; runtime.executed_words = 0;
                descriptor->entries[index].invoke(&runtime);
                check(runtime.executed_words == 0 && runtime.next_pc == runtime.guest_pc, "SPU pending stop did not yield");
                compiled_spu->state.store(bs_t<cpu_flag>{});
                reinterpret_cast<be_t<u32>*>(compiled_spu->ls + 0x100)[40] ^= 1;
                check(!aot::execute(2, input.opcode, runtime), "changed middle word of native SPU program executed");
            }
            ++comparisons;
        }
    }
    {
        const auto index = basic_count + 3; const auto& input = inputs[index];
        auto& cpu = *compiled_spu;
        cpu.state.store(bs_t<cpu_flag>{}); cpu.pc = 0x100; cpu.gpr.fill({});
        cpu.gpr[3]._u32[3] = 0x110;
        for (usz i = 0; i < input.words.size(); ++i) reinterpret_cast<be_t<u32>*>(cpu.ls + 0x100)[i] = input.words[i];
        std::array<be_t<u32>, 4> replacement{spu_instructions::AI(6, 6, 7), 0x35000000u, 0, 0};
        for (u32 i = 0; i < 16; ++i) cpu.gpr[4]._u8[i] = reinterpret_cast<const u8*>(replacement.data())[15 - i];
        runtime.cpu = &cpu; runtime.local_store = cpu.ls; runtime.guest_pc = 0x100;
        check(aot::execute(2, input.opcode, runtime), "SPU store/SYNC program dispatch");
        check(runtime.next_pc == 0x108 && runtime.executed_words == 2 && cpu.gpr[5]._u32[3] == 0 && cpu.gpr[6]._u32[3] == 0,
            "SPU SYNC executed stale following code instead of yielding");
        check(!std::memcmp(cpu.ls + 0x110, replacement.data(), 16), "SPU native store did not modify code");
        check(!aot::execute(2, input.opcode, runtime), "SPU program modification across SYNC was accepted");
    }

    {
        const auto& input = inputs[basic_count + 4]; auto& cpu = *compiled_spu;
        cpu.state.store(bs_t<cpu_flag>{}); cpu.pc = 0x100; cpu.gpr.fill({}); cpu.gpr[0]._u32[3] = 0x2000; cpu.gpr[5]._u32[3] = 1;
        for (usz i = 0; i < input.words.size(); ++i) reinterpret_cast<be_t<u32>*>(cpu.ls + 0x100)[i] = input.words[i] ? input.words[i] : 0xdeadbeefu;
        runtime.cpu = &cpu; runtime.local_store = cpu.ls; runtime.guest_pc = 0x100;
        const bool selected = aot::execute(2, input.opcode, runtime);
        check(selected && runtime.next_pc == 0x120 && cpu.gpr[4]._u32[3] == 0 && runtime.executed_words == 1,
            fmt::format("native SPU sparse program: selected=%u next=%x r4=%x r5=%x count=%u flags=%u invoke=%u", selected,
                runtime.next_pc, cpu.gpr[4]._u32[3], cpu.gpr[5]._u32[3], runtime.executed_words,
                descriptor->entries[basic_count + 4].verification_flags, !!descriptor->entries[basic_count + 4].invoke));
        reinterpret_cast<be_t<u32>*>(cpu.ls + 0x100)[8] ^= 1;
        check(!aot::execute(2, input.opcode, runtime), "native SPU sparse verification skipped changed compiled instruction");
        reinterpret_cast<be_t<u32>*>(cpu.ls + 0x100)[8] ^= 1;
        check(aot::execute(2, input.opcode, runtime), "sparse program restoration hidden by a data-hole mismatch proof");
        std::cout << "Native SPU verification: mutable data holes ignored; every translated word and restored-code proof checked\n";
    }

    {
        const auto& input = inputs[basic_count + 5]; auto& cpu = *compiled_spu;
        cpu.state.store(bs_t<cpu_flag>{}); cpu.pc = 0; cpu.gpr.fill({});
        cpu.gpr[0]._u32[3] = 0x2000; cpu.gpr[5] = v128::from32p(3);
        for (usz i = 0; i < input.words.size(); ++i) reinterpret_cast<be_t<u32>*>(cpu.ls)[i] = input.words[i];
        runtime.cpu = &cpu; runtime.local_store = cpu.ls; runtime.guest_pc = 0;
        check(aot::execute(2, input.opcode, runtime) && runtime.next_pc == 0x2000 && cpu.gpr[4]._u32[3] == 192 && runtime.executed_words == 200,
            "native SPU program at local-store zero was relocated as an unknown observation");
        std::memset(cpu.ls, 0, 0x100);
        std::cout << "Native SPU origin: real local-store zero and resumable relative code passed\n";
    }

    {
        const auto& input = inputs[basic_count + 6]; const auto clone = code + 0x2000;
        for (usz i = 0; i < input.words.size(); ++i) vm::write32(clone + i * 4, input.words[i]);
        vm::write32(0x10008, ppu_instructions::ADDI(4, 0, 99));
        compiled_ppu->state.store(bs_t<cpu_flag>{}); compiled_ppu->cia = clone; compiled_ppu->gpr[4] = 123; compiled_ppu->lr = code + 0x4000;
        runtime.cpu = compiled_ppu.get(); runtime.local_store = nullptr; runtime.memory_base = vm::g_base_addr;
        runtime.ppu_gpr = compiled_ppu->gpr; runtime.guest_pc = clone;
        check(aot::execute(1, input.opcode, runtime) && runtime.next_pc == 0x10008 && compiled_ppu->gpr[4] == 123 && runtime.executed_words == 1,
            "relocated PPU function followed a compiled relative block for an absolute guest target");
        std::cout << "Native PPU absolute exit: relocated function returns to the actual guest target for code revalidation\n";
    }
    for (usz index = spu_absolute_index; index < spu_absolute_index + 2; ++index)
    {
        const auto& input = inputs[index];
        const u32 alias = 0x1000;
        for (usz i = 0; i < input.words.size(); ++i) reinterpret_cast<be_t<u32>*>(compiled_spu->ls + alias)[i] = input.words[i];
        for (u32 sample = 0; sample < 128; ++sample)
        {
            reference_spu->state.store({}); compiled_spu->state.store({});
            reference_spu->pc = compiled_spu->pc = alias;
            for (auto& reg : reference_spu->gpr) reg = v128::from64(random(), random());
            compiled_spu->gpr = reference_spu->gpr;
            if (spu_interpreter.decode(input.opcode)(*reference_spu, {input.opcode})) reference_spu->pc += 4;
            runtime.cpu = compiled_spu.get(); runtime.local_store = compiled_spu->ls; runtime.memory_base = vm::g_base_addr;
            runtime.guest_pc = alias; runtime.spu_gpr_words = reinterpret_cast<u32*>(compiled_spu->gpr.data());
            check(aot::execute(2, input.opcode, runtime) && runtime.next_pc == reference_spu->pc && runtime.next_pc == 0x108 &&
                runtime.executed_words == 1 && !std::memcmp(reference_spu->gpr.data(), compiled_spu->gpr.data(), sizeof(reference_spu->gpr)),
                "relocated SPU absolute branch/link followed a compiled relative target or changed registers");
            ++comparisons;
        }
    }
    std::cout << "Native SPU absolute aliases: BRA/BRASL preserve literal targets and link registers across256 interpreter comparisons\n";
    std::cout << "Native functions/programs: >32 words, CFG loops, dynamic counts, entry offsets, interrupt/budget exits, indirect returns and store/SYNC code changes passed\n";
    // Inject an asynchronous interrupt just after the real file helper has
    // returned a value. Exercise the production outer interpreter boundary:
    // it must commit the translated register/PC first and unwind safely.
    {
        const auto found = std::find_if(descriptor->imports, descriptor->imports + descriptor->import_count,
            [](const char* name) { return std::string_view(name) == "spu_read_channel"; });
        check(found != descriptor->imports + descriptor->import_count, "file read helper not emitted");
        const auto index = found - descriptor->imports;
        static const void* read_helper;
        read_helper = imports[index];
        imports[index] = reinterpret_cast<const void*>(+[](spu_thread* cpu, u32 ch) -> u32
        {
            check(!cpu->allow_interrupts_in_cpu_work, "file helper allows premature generic interrupt");
            const auto value = reinterpret_cast<u32 (*)(spu_thread*, u32)>(reinterpret_cast<std::uintptr_t>(read_helper))(cpu, ch);
            cpu->set_events(SPU_EVENT_S1);
            cpu->state += cpu_flag::pending;
            return value;
        });
        auto& cpu = *compiled_spu;
        cpu.state.store(bs_t<cpu_flag>{}); cpu.pc = 0x100; cpu.srr0 = 0;
        cpu.allow_interrupts_in_cpu_work = true; cpu.interrupt_requires_escape = true;
        cpu.interrupts_enabled = true; cpu.cpu_work_iteration_count = 0;
        spu_thread::ch_events_t events{}; events.mask = SPU_EVENT_S1; cpu.ch_events.store(events);
        cpu.ch_in_mbox.push(SPU_EVENT_S1);
        reinterpret_cast<be_t<u32>*>(cpu.ls + 0x100)[0] = spu_rr(0xd, 4, SPU_RdInMbox, 0);
        rpcs3::ios::run_static_interpreter([&] { spu_recompiler_base::old_interpreter(cpu, cpu.ls, nullptr); });
        check(cpu.gpr[4]._u32[3] == SPU_EVENT_S1 && cpu.srr0 == 0x104 && cpu.pc == 0 &&
            !cpu.interrupts_enabled && cpu.allow_interrupts_in_cpu_work,
            "file SPU interrupt lost result/next PC or permission restoration");
        imports[index] = read_helper;
        cpu.state.store(bs_t<cpu_flag>{}); cpu.ch_events.store({}); cpu.allow_interrupts_in_cpu_work = false;
        std::cout << "File SPU outer boundary: translated result/next PC commit before interrupt, permission restored\n";
    }
    // Native approximate LLVM and the static interpreter deliberately differ
    // on some extended SPU values. Test those separately against the existing
    // LLVM diagnostic reference, rather than mislabeling them as agreement.
    constexpr u32 extended[] = {0, 0x80000000, 1, 0x807fffff, 0x00800000, 0x7f7fffff, 0x7f800000, 0xff800000, 0x7fc00001, 0x3f800000, 0xbf800000};
    reinterpret_cast<be_t<u32>*>(compiled_spu->ls + 0x100)[0] = spu_rr(0x2c2);
    runtime.cpu = compiled_spu.get(); runtime.local_store = compiled_spu->ls; runtime.memory_base = vm::g_base_addr;
    runtime.guest_pc = 0x100; runtime.next_pc = 0x104;
    for (u32 sample = 0; sample < 128; ++sample)
    {
        std::array<u32, 4> expected{};
        for (u32 lane = 0; lane < 4; ++lane)
        {
            const auto a = extended[(sample + lane) % std::size(extended)];
            const auto b = extended[(sample / std::size(extended) + lane) % std::size(extended)];
            compiled_spu->gpr[5]._u32[lane] = a; compiled_spu->gpr[6]._u32[lane] = b;
            expected[lane] = spu_float_diagnostics::reference(spu_float_diagnostics::operation::fcgt, a, b, 0);
        }
        check(aot::execute({2, spu_rr(0x2c2), {}}, runtime), "extended FCGT dispatch");
        for (u32 lane = 0; lane < 4; ++lane) check(compiled_spu->gpr[4]._u32[lane] == expected[lane], "extended native LLVM comparison mismatch");
    }
    std::cout << "Extended SPU FCGT: 512 lanes match existing approximate LLVM diagnostic reference\n";
    std::fesetround(FE_TONEAREST); gv_unset_zeroing_denormals();
    // The JIT shortcut for a constant syscall number reads JIT decoder state.
    // File output must always pass the number explicitly to the native helper.
    const auto syscall = std::find_if(descriptor->imports, descriptor->imports + descriptor->import_count,
        [](const char* name) { return std::string_view(name) == "__syscall"; });
    check(syscall != descriptor->imports + descriptor->import_count, "constant AOT syscall used the JIT shortcut");
    const auto syscall_index = syscall - descriptor->imports;
    const auto saved_syscall = imports[syscall_index];
    imports[syscall_index] = reinterpret_cast<const void*>(+[](ppu_thread* cpu, u64 number)
    {
        check(number == 42, "AOT syscall number was not passed"); cpu->gpr[3] = number; cpu->cia += 4;
    });
    const auto& syscall_unit = inputs[basic_count - 1];
    for (usz i = 0; i < syscall_unit.words.size(); ++i) vm::write32(code + i * 4, syscall_unit.words[i]);
    runtime.cpu = compiled_ppu.get(); runtime.ppu_gpr = compiled_ppu->gpr; runtime.local_store = nullptr;
    runtime.memory_base = vm::g_base_addr; runtime.guest_pc = code;
    check(aot::execute({1, syscall_unit.opcode, {}}, runtime) && compiled_ppu->gpr[3] == 42 && runtime.next_pc == code + 8,
        "AOT native syscall continuation failed");
    {
        const auto& input = inputs[ppu_resume_index];
        for (usz i = 0; i < input.words.size(); ++i) vm::write32(code + i * 4, input.words[i]);
        auto missing_resume = descriptor->entries[ppu_resume_index]; missing_resume.alternate_count = 0;
        aot::select_entries(std::span(&missing_resume, 1));
        runtime.cpu = compiled_ppu.get(); runtime.local_store = nullptr; runtime.memory_base = vm::g_base_addr; runtime.guest_pc = code + 8;
        check(!aot::execute(1, input.words[2], runtime), "PPU missing-resume negative control");
        aot::select_entries(std::span(descriptor->entries + ppu_resume_index, 1));
        for (u32 sample = 0; sample < 128; ++sample)
        {
            compiled_ppu->state.store({}); compiled_ppu->lr = code + 0x1000;
            runtime.cpu = compiled_ppu.get(); runtime.ppu_gpr = compiled_ppu->gpr; runtime.local_store = nullptr;
            runtime.memory_base = vm::g_base_addr; runtime.guest_pc = code;
            check(aot::execute(1, input.opcode, runtime) && runtime.next_pc == code + 8, "PPU syscall boundary exit");
            runtime.guest_pc = runtime.next_pc;
            check(aot::execute(1, input.words[2], runtime) && compiled_ppu->gpr[4] == 49 && runtime.next_pc == code + 0x1000,
                "PPU function did not resume after its helper without interpreter execution");
            ++comparisons;
        }
        vm::write32(code, input.opcode ^ 4); runtime.guest_pc = code + 8;
        check(!aot::execute(1, input.words[2], runtime), "PPU resume bypassed earlier modified code");
    }
    imports[syscall_index] = saved_syscall;
    {
        const auto& input = inputs[spu_resume_index];
        auto* guest = reinterpret_cast<be_t<u32>*>(compiled_spu->ls + 0x100);
        for (usz i = 0; i < input.words.size(); ++i) guest[i] = input.words[i];
        auto missing_resume = descriptor->entries[spu_resume_index]; missing_resume.alternate_count = 0;
        aot::select_entries(std::span(&missing_resume, 1));
        runtime.cpu = compiled_spu.get(); runtime.local_store = compiled_spu->ls; runtime.guest_pc = 0x104;
        check(!aot::execute(2, input.words[1], runtime), "SPU missing-resume negative control");
        aot::select_entries(std::span(descriptor->entries + spu_resume_index, 1));
        for (u32 sample = 0; sample < 128; ++sample)
        {
            compiled_spu->state.store({}); compiled_spu->gpr[0]._u32[3] = 0x2000;
            compiled_spu->ch_in_mbox.clear(); compiled_spu->ch_in_mbox.push(sample);
            runtime.cpu = compiled_spu.get(); runtime.local_store = compiled_spu->ls; runtime.memory_base = vm::g_base_addr;
            runtime.spu_gpr_words = reinterpret_cast<u32*>(compiled_spu->gpr.data()); runtime.guest_pc = 0x100;
            check(aot::execute(2, input.opcode, runtime) && runtime.next_pc == 0x104, "SPU channel boundary exit");
            runtime.guest_pc = runtime.next_pc;
            check(aot::execute(2, input.words[1], runtime) && compiled_spu->gpr[5]._u32[3] == sample + 7 && runtime.next_pc == 0x2000,
                "SPU program did not resume after its channel without interpreter execution");
            ++comparisons;
        }
        guest[0] = input.opcode ^ 4; runtime.guest_pc = 0x104;
        check(!aot::execute(2, input.words[1], runtime), "SPU resume bypassed earlier modified code");
    }
    aot::select_entries(std::span(descriptor->entries, descriptor->entry_count));
    std::cout << "Native helper continuations: 128 PPU syscall and128 SPU channel resumes execute the compiled suffix; earlier code changes reject both\n";
    // Unwind a native interpreter escape through the file-loaded C entry and
    // its import thunk, with the same boundary used by the SPU worker.
    const auto helper = std::find_if(descriptor->imports, descriptor->imports + descriptor->import_count,
        [](const char* name) { return std::string_view(name) == "spu_read_channel"; });
    check(helper != descriptor->imports + descriptor->import_count, "SPU helper not emitted");
    const auto helper_index = helper - descriptor->imports;
    const auto saved_helper = imports[helper_index];
    imports[helper_index] = reinterpret_cast<const void*>(+[](spu_thread*, u32) -> u32 { rpcs3::ios::escape_static_interpreter(); });
    runtime.cpu = compiled_spu.get(); runtime.local_store = compiled_spu->ls;
    runtime.guest_pc = 0x100; runtime.next_pc = 0x104;
    const auto& abortable = *std::find_if(inputs.begin(), inputs.end(), [](const auto& item) { return item.guest_kind == 2 && item.opcode == spu_rr(0xd, 4, SPU_RdInMbox, 0); });
    reinterpret_cast<be_t<u32>*>(compiled_spu->ls + 0x100)[0] = abortable.opcode;
    bool escaped = true;
    rpcs3::ios::run_static_interpreter([&] { aot::execute({2, abortable.opcode, {}}, runtime); escaped = false; });
    check(escaped, "SPU native escape did not unwind the loaded code");
    imports[helper_index] = saved_helper;
    // A changed later word must reject the complete block before it executes.
    const auto& modified = inputs.front();
    for (usz i = 0; i < modified.words.size(); ++i) vm::write32(code + i * 4, modified.words[i]);
    vm::write32(code + 4, modified.words[1] ^ 1);
    runtime.cpu = compiled_ppu.get(); runtime.ppu_gpr = compiled_ppu->gpr;
    runtime.local_store = nullptr; runtime.memory_base = vm::g_base_addr; runtime.guest_pc = code;
    const auto before = aot::statistics();
    check(!aot::execute({1, modified.opcode, {}}, runtime), "modified code executed a stale signed block");
    const auto pending = aot::discoveries();
    check(std::any_of(pending.begin(), pending.end(), [&](const auto& item) { return item.words.size() > 1 && item.words[1] == (modified.words[1] ^ 1); }), "discovery lost modified later words");
    const auto after = aot::statistics();
    check(after.executed_ppu == before.executed_ppu && after.fallback_ppu == before.fallback_ppu + 1, "modified code dispatch accounting");
    // Warm the PC lookup explicitly, then invalidate a later word, the first
    // word, and finally the selected generation. No cached pointer authorizes
    // executing a stale signed entry in any of those cases.
    for (usz i = 0; i < modified.words.size(); ++i) vm::write32(code + i * 4, modified.words[i]);
    for (u32 i = 0; i < 2; ++i) check(aot::execute({1, modified.opcode, {}}, runtime), "warm PC cache");
    vm::write32(code + 8, modified.words[2] ^ 1);
    check(!aot::execute({1, modified.opcode, {}}, runtime), "cached entry ignored modified final word");
    vm::write32(code + 8, modified.words[2]); vm::write32(code, modified.opcode ^ 1);
    check(!aot::execute({1, modified.opcode ^ 1, {}}, runtime), "cached entry ignored modified first word");
    vm::write32(code, modified.opcode);
    aot::select_entries({});
    check(!aot::execute({1, modified.opcode, {}}, runtime), "PC cache survived generation change");
    aot::select_entries(std::span(descriptor->entries, descriptor->entry_count));
    check(aot::execute({1, modified.opcode, {}}, runtime), "replacement generation did not execute");
    std::cout << "PC dispatch cache: later/first guest-word changes and generation replacement passed\n";
    {
        static auto underlying_reader = runtime.read_code;
        static u64 read_calls = 0;
        static u32 reader_kind = 0;
        static bool bounded_reader = false;
        runtime.read_code = [](const rpcs3_aot_runtime_context_v2* state, u32* words, u32 capacity)
        {
            ++read_calls;
            const auto count = underlying_reader(state, words, capacity);
            if (bounded_reader)
                for (u32 i = 0; i < count; ++i)
                    if (aot::terminates_unit(reader_kind, words[i])) return i + 1;
            return count;
        };
        for (const u32 kind : {1u, 2u})
        {
            aot::select_entries(std::span(descriptor->entries, descriptor->entry_count));
            const auto& input = inputs[kind == 1 ? 0 : 14];
            runtime.cpu = kind == 1 ? static_cast<void*>(compiled_ppu.get()) : static_cast<void*>(compiled_spu.get());
            runtime.ppu_gpr = compiled_ppu->gpr; runtime.spu_gpr_words = reinterpret_cast<u32*>(compiled_spu->gpr.data());
            runtime.local_store = kind == 2 ? compiled_spu->ls : nullptr;
            runtime.memory_base = vm::g_base_addr; runtime.guest_pc = kind == 1 ? code : 0x100;
            auto* guest = reinterpret_cast<be_t<u32>*>((runtime.local_store ? compiled_spu->ls : vm::g_base_addr) + runtime.guest_pc);
            for (u32 i = 0; i < 32; ++i) guest[i] = i < input.words.size() ? input.words[i] : 0;
            guest[1] = input.words[1] ^ 1;
            read_calls = 0; bounded_reader = false; reader_kind = kind;
            for (u32 i = 0; i < 3; ++i) check(!aot::execute({kind, input.opcode, {}}, runtime), "missing unit unexpectedly executed");
            auto stats = aot::statistics();
            check(read_calls == 1 && stats.pending == 1 && (kind == 1 ? stats.fallback_ppu : stats.fallback_spu) == 3,
                "repeated miss rescanned code, lost discovery or changed fallback accounting");
            // Same first opcode, but restoring a later word makes this a
            // compiled unit again. A negative cache must not hide that change.
            guest[1] = input.words[1];
            check(aot::execute({kind, input.opcode, {}}, runtime) && read_calls == 1, "restored native code was not selected without a new discovery scan");
            u32 unknown = 0;
            for (u32 prefix = 0; prefix < 2048; ++prefix)
                if (!aot::known_instruction(kind, prefix << 21)) { unknown = prefix << 21; break; }
            check(!aot::known_instruction(kind, unknown), "unknown-opcode boundary fixture");
            const u32 missing = kind == 1 ? ppu_instructions::ADDI(4, 5, 123) : spu_instructions::AI(4, 5, 123);
            guest[0] = missing; guest[1] = unknown; guest[2] = unknown;
            bounded_reader = true; read_calls = 0;
            const auto before = aot::statistics();
            for (u32 i = 0; i < 2; ++i) check(!aot::execute({kind, missing, {}}, runtime), "unknown-boundary fallback");
            check(read_calls == 1 && aot::statistics().pending == before.pending + 1, "unknown-boundary repeated discovery");
            // Discovery omits the unknown opcode; cache validation must still
            // include it so replacing it discovers the newly extended window.
            guest[1] = missing;
            check(!aot::execute({kind, missing, {}}, runtime) && read_calls == 2 && aot::statistics().pending == before.pending + 2,
                "miss cache ignored a changed unknown boundary");
            aot::select_entries({});
            check(!aot::execute({kind, missing, {}}, runtime) && read_calls == 3 && aot::statistics().pending == 1,
                "miss cache survived generation replacement");
        }
        // More guest PCs than the former direct-map capacity must not force
        // full-prefix lookup on every traversal of a stable native CPU loop.
        for (const u32 kind : {1u, 2u})
        {
            aot::select_entries(std::span(descriptor->entries, descriptor->entry_count));
            const u32 missing = kind == 1 ? ppu_instructions::ADDI(4, 5, 123) : spu_instructions::AI(4, 5, 123);
            runtime.cpu = kind == 1 ? static_cast<void*>(compiled_ppu.get()) : static_cast<void*>(compiled_spu.get());
            runtime.local_store = kind == 2 ? compiled_spu->ls : nullptr;
            const auto base_pc = kind == 1 ? code : 0x100u;
            auto* guest = reinterpret_cast<be_t<u32>*>((runtime.local_store ? compiled_spu->ls : vm::g_base_addr) + base_pc);
            for (u32 i = 0; i < 1024 + 31; ++i) guest[i] = missing;
            bounded_reader = false; reader_kind = kind; read_calls = 0;
            for (u32 pass = 0; pass < 2; ++pass)
                for (u32 i = 0; i < 1024; ++i)
                {
                    runtime.guest_pc = base_pc + i * 4;
                    check(!aot::execute({kind, missing, {}}, runtime), "working-set fallback unexpectedly compiled");
                }
            const auto stats = aot::statistics();
            check(read_calls == 1024 && stats.pending == 1 &&
                (kind == 1 ? stats.fallback_ppu : stats.fallback_spu) == 2048,
                "stable 1024-PC loop rescanned after warmup or changed exact fallback/discovery counts");
            const auto& prepared = inputs[kind == 1 ? 0 : 14];
            const auto words = prepared.words.size();
            aot::select_entries(std::span(descriptor->entries, descriptor->entry_count));
            for (u32 i = 0; i < 1024; ++i)
                for (u32 word = 0; word < words; ++word) guest[i * words + word] = prepared.words[word];
            read_calls = 0;
            for (u32 pass = 0; pass < 2; ++pass)
                for (u32 i = 0; i < 1024; ++i)
                {
                    runtime.guest_pc = base_pc + i * words * 4;
                    check(aot::execute({kind, prepared.opcode, {}}, runtime), "working-set prepared unit did not execute");
                }
            const auto positive = aot::statistics();
            check(read_calls == 0 && positive.pending == 0 &&
                (kind == 1 ? positive.executed_ppu : positive.executed_spu) == 2048 * words &&
                positive.fallback_ppu == 0 && positive.fallback_spu == 0,
                "prepared 1024-PC loop rescanned after warmup or changed exact execution accounting");
        }
        // Exercise the complete SPU page directory without reading across the
        // local-store boundary, including its final page. No page may alias.
        aot::select_entries(std::span(descriptor->entries, descriptor->entry_count));
        runtime.cpu = compiled_spu.get(); runtime.local_store = compiled_spu->ls;
        const u32 missing = spu_instructions::AI(4, 5, 123);
        auto* local_code = reinterpret_cast<be_t<u32>*>(compiled_spu->ls);
        for (u32 page = 0; page < 256; ++page)
            for (u32 word = 0; word < 32; ++word) local_code[page * 256 + word] = missing;
        read_calls = 0;
        for (u32 pass = 0; pass < 2; ++pass)
            for (u32 page = 0; page < 256; ++page)
            {
                runtime.guest_pc = page * 1024;
                check(!aot::execute({2, missing, {}}, runtime), "SPU full-directory fallback");
            }
        check(read_calls == 256 && aot::statistics().fallback_spu == 512 && aot::statistics().pending == 1,
            "SPU local-store pages alias or change exact fallback accounting");
        std::cout << "AOT working set: 1024 PPU/SPU PCs and all SPU pages reuse validated hits/misses passed\n";
        runtime.read_code = underlying_reader;
        aot::select_entries(std::span(descriptor->entries, descriptor->entry_count));
        std::cout << "AOT miss cache: PPU/SPU exact fallback counts, single discovery, modified code, unknown boundaries and generation changes passed\n";
    }

    // Native address hints must accelerate only validated lookup. Wrong hints,
    // relocated code, aliases and modifications retain content-based dispatch.
    {
        static unsigned checks = 0;
        static auto native_match = runtime.matches_code;
        runtime.matches_code = +[](const rpcs3_aot_runtime_context_v2* at, const u32* words, u32 count)
        { ++checks; return native_match(at, words, count); };
        for (const u32 kind : {1u, 2u})
        {
            const u32 opcode = kind == 1 ? ppu_instructions::ADDI(4, 4, 1) : spu_instructions::AI(4, 4, 1);
            const u32 base = kind == 1 ? code : 0x100;
            std::vector<std::vector<u32>> words(65, std::vector<u32>(64, opcode));
            std::vector<rpcs3_aot_entry_v2> entries;
            std::vector<u32> origins;
            const u32 alternate = 7;
            for (u32 i = 0; i < words.size(); ++i)
            {
                words[i][1] = i + 1;
                entries.push_back({kind, opcode, 64, 0, words[i].data(), +[](rpcs3_aot_runtime_context_v2* at)
                    { at->next_pc = at->guest_pc + 4; at->executed_words = 1; }, 1, 0, &alternate});
                origins.push_back(base + i * 256);
            }
            runtime.cpu = kind == 1 ? static_cast<void*>(compiled_ppu.get()) : compiled_spu.get();
            runtime.local_store = kind == 2 ? compiled_spu->ls : nullptr;
            runtime.memory_base = vm::g_base_addr;
            const u32 pc = origins.back();
            auto* guest = reinterpret_cast<be_t<u32>*>((kind == 2 ? compiled_spu->ls : vm::g_base_addr) + pc);
            for (u32 i = 0; i < 64; ++i) guest[i] = words.back()[i];
            aot::select_entries(entries, origins); runtime.guest_pc = pc; checks = 0;
            check(aot::execute(kind, opcode, runtime) && checks == 1, "native address hint must avoid opcode-bucket scans");
            runtime.guest_pc = pc + alternate * 4; checks = 0;
            check(aot::execute(kind, opcode, runtime) && checks == 1, "native address hint must retain resumable aliases");
            guest[40] = opcode ^ 1; runtime.guest_pc = pc;
            check(!aot::execute(kind, opcode, runtime), "native address hint bypassed changed-code rejection");
            guest[40] = opcode;
            check(aot::execute(kind, opcode, runtime), "native address hint hid restored code");
            if (kind == 1)
            {
                const std::array copies{entries.back(), entries.back()};
                const std::array<u32, 2> copy_origins{pc + 0x8000, pc};
                aot::select_entries(copies, copy_origins); runtime.guest_pc = pc; checks = 0;
                check(aot::execute(kind, opcode, runtime) && checks == 1,
                    "merged word constants lost a distinct PPU descriptor origin");
            }
            origins.front() = pc; origins.back() = pc + 0x8000;
            aot::select_entries(entries, origins); runtime.guest_pc = pc;
            if (kind == 1)
            {
                checks = 0;
                check(!aot::execute(kind, opcode, runtime) && checks == 1,
                    "PPU address mismatch must not search unrelated same-opcode functions");
                runtime.guest_pc = pc + 8192; checks = 0;
                check(!aot::execute(kind, opcode, runtime) && checks == 0,
                    "unknown PPU address must fall back without guest-code scans");
                // Content-only compiler fixtures still exercise relocated lowering;
                // production PPU lookup requires loader-owned addresses.
                aot::select_entries(entries);
                runtime.guest_pc = pc;
            }
            check(aot::execute(kind, opcode, runtime), "content fixture fallback failed");
            auto* relocated = guest + 2048;
            for (u32 i = 0; i < 64; ++i) relocated[i] = words.back()[i];
            runtime.guest_pc = pc + 8192;
            check(aot::execute(kind, opcode, runtime), "relocated native code requires content-based lookup");
        }
        runtime.matches_code = native_match;
        aot::select_entries(std::span(descriptor->entries, descriptor->entry_count));
        std::cout << "Native address dispatch: one-check lookup, aliases, relocation and changed/restored code passed\n";
    }

    // Many different prepared programs may share the entry opcode. Exercise
    // the bounded negative-proof pool across enough PCs to wrap its epoch,
    // then restore a later word and require a positive selection.
    {
        const u32 opcode = ppu_instructions::ADDI(4, 4, 1);
        std::vector<std::vector<u32>> code_words(257, std::vector<u32>(64, opcode));
        std::vector<rpcs3_aot_entry_v2> candidates;
        for (u32 i = 0; i < code_words.size(); ++i)
        {
            code_words[i][1] = i + 1;
            candidates.push_back({1, opcode, 64, 0, code_words[i].data(), +[](rpcs3_aot_runtime_context_v2*) {}, 0, 0, nullptr});
        }
        aot::select_entries(candidates);
        runtime.cpu = compiled_ppu.get(); runtime.local_store = nullptr; runtime.memory_base = vm::g_base_addr;
        for (u32 i = 0; i < 512; ++i) vm::write32(code + 0x1000 + i * 4, opcode);
        for (u32 pass = 0; pass < 2; ++pass)
            for (u32 i = 0; i < 258; ++i)
            {
                runtime.guest_pc = code + 0x1000 + i * 4;
                check(!aot::execute(1, opcode, runtime), "negative proof accepted mismatching native program");
            }
        runtime.guest_pc = code + 0x1000;
        vm::write32(runtime.guest_pc + 4, 3);
        check(aot::execute(1, opcode, runtime), "restored later word was hidden by a bounded negative proof");
        vm::write32(runtime.guest_pc + 4, opcode);
        check(!aot::execute(1, opcode, runtime), "changed native code survived positive cache validation");
        aot::select_entries(std::span(descriptor->entries, descriptor->entry_count));
        std::cout << "AOT negative proofs: shared entry opcodes, bounded epoch replacement, later-word restoration and changed-code rejection passed\n";
    }
    if (std::getenv("RPCS3_AOT_DISPATCH_BENCHMARK"))
    {
        // Fixed work isolates lookup/validation/accounting around the exact
        // same file-loaded LLVM function. This is not a JIT or gameplay test.
        constexpr u32 iterations = 1000000;
        std::ofstream report(root / "dispatch-benchmark.jsonl");
        for (const usz index : {usz(0), usz(14)})
        {
            const auto& input = inputs[index]; const auto& entry = descriptor->entries[index];
            runtime.cpu = input.guest_kind == 1 ? static_cast<void*>(compiled_ppu.get()) : static_cast<void*>(compiled_spu.get());
            runtime.ppu_gpr = compiled_ppu->gpr; runtime.spu_gpr_words = reinterpret_cast<u32*>(compiled_spu->gpr.data());
            runtime.local_store = input.guest_kind == 2 ? compiled_spu->ls : nullptr;
            runtime.memory_base = vm::g_base_addr; runtime.guest_pc = input.guest_kind == 1 ? code : 0x100;
            auto* guest = reinterpret_cast<be_t<u32>*>((runtime.local_store ? compiled_spu->ls : vm::g_base_addr) + runtime.guest_pc);
            for (u32 i = 0; i < 32; ++i) guest[i] = i < input.words.size() ? input.words[i] : 0;
            check(aot::execute({input.guest_kind, input.opcode, {}}, runtime), "benchmark warmup");
            for (u32 trial = 0; trial < 7; ++trial)
                for (u32 pass = 0; pass < 2; ++pass)
                {
                    const bool dispatch = (trial + pass) % 2 == 0;
                    const auto before = aot::statistics();
                    const auto start = std::chrono::steady_clock::now();
                    for (u32 i = 0; i < iterations; ++i)
                    {
                        if (dispatch) { if (!aot::execute({input.guest_kind, input.opcode, {}}, runtime)) throw std::runtime_error("benchmark fallback"); }
                        else entry.invoke(&runtime);
                    }
                    const double nanos = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / iterations;
                    const auto after = aot::statistics();
                    const auto executed = input.guest_kind == 1 ? after.executed_ppu - before.executed_ppu : after.executed_spu - before.executed_spu;
                    check(executed == (dispatch ? u64(iterations) * input.words.size() : 0), "benchmark exact execution statistics");
                    check(after.fallback_ppu == before.fallback_ppu && after.fallback_spu == before.fallback_spu, "benchmark no fallback");
                    report << "{\"guest_kind\":" << input.guest_kind << ",\"trial\":" << trial << ",\"dispatch\":" << (dispatch ? "true" : "false")
                        << ",\"iterations\":" << iterations << ",\"words\":" << input.words.size() << ",\"nanoseconds_per_call\":" << nanos << "}\n";
                }
        }
        std::cout << "Fixed-work file-loaded dispatch benchmark passed (not JIT/gameplay)\n";
    }
    if (std::getenv("RPCS3_AOT_MISS_BENCHMARK"))
    {
        // Isolate the overhead paid before one native interpreter instruction
        // when no compiled prefix exists. This is not game throughput.
        for (const u32 sites : {1u, 1024u})
        {
            std::ofstream report(root / (sites == 1 ? "miss-benchmark.jsonl" : "miss-multi-pc.jsonl"));
            constexpr u32 iterations = 200000;
            for (const u32 kind : {1u, 2u})
            {
                const u32 opcode = kind == 1 ? ppu_instructions::ADDI(4, 5, 123) : spu_instructions::AI(4, 5, 123);
                runtime.cpu = kind == 1 ? static_cast<void*>(compiled_ppu.get()) : static_cast<void*>(compiled_spu.get());
                runtime.ppu_gpr = compiled_ppu->gpr; runtime.spu_gpr_words = reinterpret_cast<u32*>(compiled_spu->gpr.data());
                runtime.local_store = kind == 2 ? compiled_spu->ls : nullptr;
                runtime.memory_base = vm::g_base_addr; runtime.guest_pc = kind == 1 ? code : 0x100;
                auto* guest = reinterpret_cast<be_t<u32>*>((runtime.local_store ? compiled_spu->ls : vm::g_base_addr) + runtime.guest_pc);
                for (u32 i = 0; i < sites + 31; ++i) guest[i] = opcode;
                const auto base_pc = runtime.guest_pc;
                for (u32 i = 0; i < sites; ++i)
                {
                    runtime.guest_pc = base_pc + i * 4;
                    check(!aot::execute({kind, opcode, {}}, runtime), "working-set warmup must remain missing");
                }
                check(!aot::execute({kind, opcode, {}}, runtime), "benchmark unit must remain missing");
                ppu_intrp_func end{+[](ppu_thread& cpu, ppu_opcode_t, be_t<u32>* next, ppu_intrp_func*) { cpu.cia = vm::get_addr(next); }};
                for (u32 trial = 0; trial < 7; ++trial)
                    for (u32 pass = 0; pass < 2; ++pass)
                    {
                        const bool dispatch = (trial + pass) % 2 == 0;
                        const auto before = aot::statistics();
                        const auto start = std::chrono::steady_clock::now();
                        for (u32 i = 0; i < iterations; ++i)
                        {
                            runtime.guest_pc = base_pc + (i % sites) * 4;
                            if (dispatch) { if (aot::execute({kind, opcode, {}}, runtime)) throw std::runtime_error("unexpected compiled miss benchmark"); }
                            else if (kind == 1) ppu_interpreter.decode(opcode)(*compiled_ppu, {opcode}, guest + i % sites, &end);
                            else spu_interpreter.decode(opcode)(*compiled_spu, {opcode});
                        }
                        const double nanos = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / iterations;
                        const auto after = aot::statistics();
                        check((kind == 1 ? after.fallback_ppu - before.fallback_ppu : after.fallback_spu - before.fallback_spu) == (dispatch ? iterations : 0), "miss benchmark exact fallback accounting");
                        check(after.executed_ppu == before.executed_ppu && after.executed_spu == before.executed_spu && after.pending == before.pending, "miss benchmark stable discovery and no AOT execution");
                        report << "{\"guest_kind\":" << kind << ",\"trial\":" << trial << ",\"dispatch\":" << (dispatch ? "true" : "false")
                            << ",\"sites\":" << sites << ",\"iterations\":" << iterations << ",\"words\":32,\"nanoseconds_per_call\":" << nanos << "}\n";
                    }
            }
        }
        std::cout << "Fixed-work AOT miss overhead/native interpreter control passed\n";
    }
    if (std::getenv("RPCS3_AOT_COMPILE_BENCHMARK"))
    {
        std::ofstream report(root / "compile-benchmark.jsonl");
        std::vector<char> reference;
        std::string error;
        check(aot::emit_instructions(inputs, options, reference, error), error);
        for (u32 trial = 0; trial < 7; ++trial)
            for (u32 pass = 0; pass < 2; ++pass)
            {
                const u32 workers = (trial + pass) % 2 ? 2 : 1;
                std::array<std::vector<char>, 8> objects;
                const auto start = std::chrono::steady_clock::now();
                aot::preparation_tasks(objects.size(), workers, [&](std::size_t i)
                {
                    std::string failure;
                    check(aot::emit_instructions(inputs, options, objects[i], failure), failure);
                });
                const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                for (const auto& object : objects) check(object == reference, "timed parallel/serial compiler output differs");
                report << "{\"trial\":" << trial << ",\"workers\":" << workers << ",\"batches\":" << objects.size()
                    << ",\"entries_per_batch\":" << inputs.size() << ",\"seconds\":" << seconds << "}\n";
            }
        std::cout << "Fixed-input compiler benchmark: identical serial/parallel LLVM objects passed (excludes analysis/link/sign)\n";
    }
    check(rpcs3::ios::jit::rejected_executable_requests() == 0 && rpcs3::ios::jit::rejected_preparation_requests() == 0, "native AOT made a CPU JIT request");
    std::cout << "Native PPU/SPU translators vs native interpreters: " << comparisons << " comparisons, helper imports=" << imports.size() << ", CPU JIT requests=0/0\n";
    compiled_spu.reset(); reference_spu.reset(); compiled_ppu.reset(); reference_ppu.reset();
    g_fxo->clear();
    test_aot_native_analysis_after_stop();
    // Loaded code remains mapped for the process lifetime, matching the app.
    return 0;
}
catch (const aot::failure& error) { std::cerr << error.message << '\n'; return 1; }
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
