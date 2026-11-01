#include "Emu/Cell/lv2/sys_sync.h"
#include "ios/AOTAnalysis.h"
#include "ios/AOTDispatch.h"
#include "ios/AOTError.h"
#include "Emu/IdManager.h"
#include "Loader/ELF.h"
#include "Loader/PSF.h"
#include "Crypto/unself.h"
#include "Utilities/File.h"
#include "Emu/Cell/SPUOpcodes.h"
#include "Emu/Cell/SPURecompiler.h"
#include "Emu/Cell/PPUAnalyser.h"
#include <algorithm>
#include <iostream>
#include <random>
#include <stdexcept>
#include <source_location>
#include <filesystem>

namespace rpcs3::ios::aot { std::vector<instruction> test_native_aot_game_entries(const std::string&, const std::string&); }

namespace
{
void verify(bool value, std::source_location where = std::source_location::current()) { if (!value) throw std::runtime_error("Native AOT executable analysis regression at line " + std::to_string(where.line())); }
template <typename Size, elf_machine Machine>
std::vector<u8> image(u32 address, std::initializer_list<u32> words, bool executable = true)
{
    elf_object<elf_be, Size, Machine, elf_os::none, elf_type::none> elf;
    elf.header.e_type = elf_type::exec;
    elf.header.e_entry = address;
    auto& segment = elf.progs.emplace_back();
    segment.p_type = 1; segment.p_flags = executable ? 5 : 4;
    segment.p_vaddr = address; segment.p_memsz = segment.p_filesz = words.size() * 4;
    segment.bin.resize(words.size() * 4);
    usz offset = 0;
    for (auto word : words) { const be_t<u32> encoded = word; std::memcpy(segment.bin.data() + offset, &encoded, 4); offset += 4; }
    elf.set_error(elf_error::ok);
    return elf.save();
}
}
void test_aot_native_analysis()
{
    namespace aot = rpcs3::ios::aot;
    {
        // A loaded/relocated module uses the same extraction as offline files.
        std::array<be_t<u32>, 48> words;
        for (auto& word : words) word = 0x38630001;
        ppu_module<lv2_obj> module;
        module.segs.push_back({0x32000, sizeof(words), 1, 5, sizeof(words), words.data()});
        module.addr_to_seg_index.emplace(0x32000, 0);
        auto& function = module.funcs.emplace_back(); function.addr = 0x32000; function.size = sizeof(words);
        function.blocks.emplace(0x32000, 96); function.blocks.emplace(0x32060, 96);
        aot::instruction_inventory native;
        aot::collect_ppu_functions(module, native);
        verify(native.size() == 1 && native.begin()->first.address == 0x32000 && native.begin()->first.words.size() == 48 && native.begin()->first.blocks.size() == 2);
        aot::observe_ppu_functions(module);
        const auto observed = aot::discoveries();
        verify(std::find(observed.begin(), observed.end(), native.begin()->first) != observed.end());
        std::cout << "Loaded native PPU functions: relocated origin, >32 words and native blocks retained\n";
    }
    {
        // Executable PT_LOAD also contains data. Share the normal loader's
        // section boundary and retain non-executable sections for OPD analysis.
        ppu_module<lv2_obj> module;
        module.segs.push_back({0x10000, 0x1000, 1, 5, 0x1000});
        verify(module.add_analysis_section(1, 0x10000, 0x100, 6) == 0x10100);
        verify(module.add_analysis_section(1, 0x10100, 0x900, 2) == 0);
        verify(module.secs.size() == 2 && module.secs.back().flags == 2);
        verify(module.add_analysis_section(8, 0x10200, 0x100, 6) == 0);
        verify(module.add_analysis_section(1, 0xfffffff0ull, 0x20, 6) == 0);
        verify(module.add_analysis_section(1, 0x100000000ull, 4, 6) == 0);
        verify(module.secs.size() == 2);
        verify(module.add_analysis_section(1, 0x20000, 0x100, 6) == 0);
        verify(module.secs.size() == 3); // Other segments do not extend segment zero.
    }
    {
        // Production discovery must follow the installed update's boot choice,
        // without suppressing unrelated disc modules or trusting another title.
        const auto root = std::filesystem::path(fs::get_config_dir());
        const auto disc = root / "aot-update-fixture";
        const auto update = root / "dev_hdd0/game/BLUS00000";
        std::filesystem::create_directories(disc / "PS3_GAME/USRDIR");
        std::filesystem::create_directories(update / "USRDIR");
        const auto write = [](const auto& path, const auto& bytes)
        {
            fs::file file(path.string(), fs::rewrite);
            verify(file && file.write(bytes.data(), bytes.size()) == bytes.size());
        };
        write(disc / "PS3_GAME/USRDIR/EBOOT.BIN", image<u64, elf_machine::ppc64>(0, {0x38630001, 0x4e800020}));
        write(disc / "PS3_GAME/USRDIR/worker.self", image<u64, elf_machine::ppc64>(0, {0x38630003, 0x4e800020}));
        write(update / "USRDIR/EBOOT.BIN", image<u64, elf_machine::ppc64>(0, {0x38630002, 0x4e800020}));
        const auto check = [&](const char* title, const char* category, bool base_expected)
        {
            psf::registry sfo;
            psf::assign(sfo, "TITLE_ID", psf::entry{psf::format::string, 16, title});
            psf::assign(sfo, "CATEGORY", psf::entry{psf::format::string, 4, category});
            write(update / "PARAM.SFO", psf::save_object(sfo));
            const auto found = aot::test_native_aot_game_entries(disc.string(), "BLUS00000");
            const auto contains = [&](u32 op) { return std::any_of(found.begin(), found.end(), [&](const auto& entry) { return entry.opcode == op; }); };
            verify(contains(0x38630001) == base_expected);
            verify(contains(0x38630002) && contains(0x38630003));
        };
        check("BLUS00000", "GD", false);
        check("BLUS00001", "GD", true);
        check("BLUS00000", "DG", true);
        std::filesystem::remove_all(disc);
        std::filesystem::remove_all(update);
    }
    const auto ppu = image<u64, elf_machine::ppc64>(0, {0x38630001, 0x38630002, 0x4e800020});
    const auto spu = image<u32, elf_machine::spu>(0x100, {0x40800003, 0x40800004, 0x32000000});
    aot::instruction_inventory inventory;
    auto result = aot::analyze_elf(ppu, inventory);
    verify(result.ppu_images == 1 && !inventory.empty() && inventory.begin()->first.words.size() == 3);
    inventory.clear(); result = aot::analyze_elf(spu, inventory);
    verify(result.spu_images == 1 && !inventory.empty());
    for (const auto& [unit, count] : inventory) verify(unit.guest_kind == 2 && !unit.words.empty());
    for (auto bytes : {image<u64, elf_machine::ppc64>(0, {0x38630001}, false),
                       image<u32, elf_machine::spu>(0x100, {0x40800003}, false)})
    {
        inventory.clear(); aot::analyze_elf(bytes, inventory); verify(inventory.empty());
    }
    elf_object<elf_be, u32, elf_machine::spu, elf_os::none, elf_type::none> outside_ls;
    verify(outside_ls.open(fs::make_stream(std::vector<u8>(spu))) == elf_error::ok);
    outside_ls.progs[0].p_memsz = 0x40001;
    inventory.clear(); result = aot::analyze_elf(outside_ls.save(), inventory);
    verify(result.spu_images == 0 && inventory.empty());
    {
        // Go through the real ELF writer/reader, including big-endian section
        // types. Treating sh_type's stored bytes as a host integer loses .text.
        ppu_exec_object sectioned;
        verify(sectioned.open(fs::make_stream(image<u64, elf_machine::ppc64>(0x10000, {0x4e800020}))) == elf_error::ok);
        sectioned.header.e_entry = 0x10100;
        auto& code = sectioned.progs.front();
        code.bin.resize(0x200);
        code.p_filesz = code.p_memsz = code.bin.size();
        const be_t<u32> descriptor[] = {0x10000, 0x10180};
        std::memcpy(code.bin.data() + 0x100, descriptor, sizeof(descriptor));
        auto& text = sectioned.shdrs.emplace_back();
        text.sh_type = sec_type::sht_progbits; text.sh_addr = 0x10000;
        text.sh_size = 4; text._sh_flags = 6;
        auto& data = sectioned.shdrs.emplace_back();
        data.sh_type = sec_type::sht_progbits; data.sh_addr = 0x10004;
        data.sh_size = 0x1fc; data._sh_flags = 2;
        inventory.clear(); result = aot::analyze_elf(sectioned.save(), inventory);
        verify(result.ppu_images == 1 && !inventory.empty());
        for (const auto& [unit, count] : inventory)
            verify(unit.guest_kind != 1 || (unit.address == 0x10000 && unit.words.size() == 1));
    }
    // Use the native writer for the enclosing file, too: embedded SPU images
    // are ordinary bytes inside a PPU PT_LOAD segment.
    elf_object<elf_be, u64, elf_machine::ppc64, elf_os::none, elf_type::none> container;
    container.header.e_type = elf_type::exec;
    auto& segment = container.progs.emplace_back();
    segment.p_type = 1; segment.p_flags = 5; segment.p_vaddr = 0;
    segment.bin.resize(16); segment.bin.insert(segment.bin.end(), spu.begin(), spu.end());
    segment.p_memsz = segment.p_filesz = segment.bin.size();
    container.set_error(elf_error::ok);
    inventory.clear(); result = aot::analyze_elf(container.save(), inventory);
    verify(result.ppu_images == 1 && result.spu_images == 1);
    inventory.clear(); result = aot::analyze_elf(container.save(), inventory, {}, false);
    verify(result.ppu_images == 1 && result.spu_images == 1 && !inventory.empty());
    verify(std::all_of(inventory.begin(), inventory.end(), [](const auto& item) { return item.first.guest_kind == 2; }));
    inventory.clear(); result = aot::analyze_elf(image<u64, elf_machine::ppc64>(0x10000, {0x38630001, 0x4e800020}), inventory, {}, false);
    verify(result.ppu_images == 1 && result.spu_images == 0 && inventory.empty());

    // Cancellation crosses the real scanner and SPU compiler frames, retaining
    // an owned error payload rather than an exception-inheritance assumption.
    u32 cancel_checks = 0; bool spu_cancelled = false;
    try { aot::analyze_elf(spu, inventory, [&] { if (++cancel_checks == 2) throw aot::failure{"cancelled native SPU analysis"}; }); }
    catch (const aot::failure& error) { spu_cancelled = error.message == "cancelled native SPU analysis"; }
    verify(spu_cancelled);
    bool scanner_cancelled = false;
    try { ppu_find_spu_code(spu, {}, {}, [&] { if (!scanner_cancelled) throw aot::failure{"cancelled shared scanner"}; }); }
    catch (const aot::failure& error) { scanner_cancelled = error.message == "cancelled shared scanner"; }
    verify(scanner_cancelled);
    // A startup entry may return before worker functions are called. Native
    // call-target discovery must add the separate worker, not only entry 0.
    const u32 brsl = (0x66u << 23) | (8u << 7); // relative call +32, LR=0
    const u32 bi_lr = 0x35000000;
    const auto workers = image<u32, elf_machine::spu>(0x100,
        {bi_lr, 0, 0, 0, brsl, bi_lr, 0, 0, 0, 0, 0, 0,
         spu_instructions::AI(7, 7, 71), bi_lr});
    inventory.clear(); result = aot::analyze_elf(workers, inventory);
    const auto worker = spu_instructions::AI(7, 7, 71);
    verify(std::any_of(inventory.begin(), inventory.end(), [&](const auto& item)
        { return item.first.guest_kind == 2 && item.first.opcode == worker; }));
    auto raw = std::vector<be_t<u32>>{brsl, bi_lr, 0, 0, 0, 0, 0, 0, worker, bi_lr};
    inventory.clear();
    aot::analyze_spu_segment(0x4000,
        {reinterpret_cast<const u8*>(raw.data()), raw.size() * 4}, inventory);
    verify(std::any_of(inventory.begin(), inventory.end(), [&](const auto& item)
        { return item.first.guest_kind == 2 && item.first.opcode == worker; }));
    // The shared raw scanner preserves its ELF callback boundary and respects
    // cancellation before examining a candidate. No guest state is touched.
    u32 elf_matches = 0;
    ppu_find_spu_code(std::span<const u8>(spu), {}, [&](u32 offset) { verify(offset == 0); ++elf_matches; return u32(spu.size()); });
    verify(elf_matches == 1);
    // Exercise the actual native SPURS/roaming-code miner, not only direct
    // segment collection. The program deliberately has no ELF header.
    std::vector<be_t<u32>> roaming(0x120 / 4, spu_instructions::NOP());
    for (u32 i = 0; i < 4; ++i) roaming[i] = spu_instructions::ILA(2, 0x100 + i * 4);
    roaming[0x10 / 4] = (0x66u << 23) | (4u << 7); // call +16
    roaming[0x14 / 4] = bi_lr;
    for (u32 at : {0x20u, 0x70u, 0xc0u})
    {
        roaming[at / 4] = spu_instructions::STQD(0, 1, 16);
        roaming[at / 4 + 1] = spu_instructions::AI(1, 1, -32);
        roaming[at / 4 + 2] = bi_lr;
    }
    elf_object<elf_be, u64, elf_machine::ppc64, elf_os::none, elf_type::none> raw_container;
    raw_container.header.e_type = elf_type::exec;
    auto& raw_code = raw_container.progs.emplace_back();
    raw_code.p_type = 1; raw_code.p_flags = 5;
    raw_code.bin = {0x4e, 0x80, 0x00, 0x20}; raw_code.p_memsz = raw_code.p_filesz = 4;
    auto& raw_data = raw_container.progs.emplace_back();
    raw_data.p_type = 1; raw_data.p_flags = 4; raw_data.p_vaddr = 0x20000;
    raw_data.bin.resize(roaming.size() * 4); std::memcpy(raw_data.bin.data(), roaming.data(), raw_data.bin.size());
    raw_data.p_memsz = raw_data.p_filesz = raw_data.bin.size(); raw_container.set_error(elf_error::ok);
    inventory.clear(); result = aot::analyze_elf(raw_container.save(), inventory);
    verify(result.spu_images == 1);
    verify(std::any_of(inventory.begin(), inventory.end(), [&](const auto& item)
        { return item.first.guest_kind == 2 && item.first.opcode == spu_instructions::STQD(0, 1, 16); }));
    bool cancelled = false;
    try { aot::analyze_elf(ppu, inventory, [&] { if (!inventory.empty()) throw std::runtime_error("cancelled"); }); }
    catch (const std::exception& error) { cancelled = std::string(error.what()) == "cancelled"; }
    verify(cancelled);
    for (const auto& original : {ppu, spu})
        for (usz length = 0; length < original.size(); ++length)
        {
            inventory.clear(); aot::analyze_elf(std::span(original).first(length), inventory); verify(inventory.empty());
        }
    auto endian = ppu; endian[5] = 1;
    inventory.clear(); verify(aot::analyze_elf(endian, inventory).ppu_images == 0 && inventory.empty());
    std::mt19937 random(0xa07e1f);
    for (unsigned sample = 0; sample < 1000; ++sample)
    {
        auto bytes = sample & 1 ? ppu : spu;
        for (unsigned edit = 0; edit < 8; ++edit) bytes[random() % bytes.size()] = u8(random());
        inventory.clear(); aot::analyze_elf(bytes, inventory);
    }
    std::cout << "Native ELF/analyser tests: PPU/SPU, embedded programs, permissions, truncation, cancellation, 1000 damaged inputs passed\n";
}

void test_aot_native_game_analysis(const std::string& file)
{
    auto input = decrypt_self(fs::file(file));
    verify(input && input.size() <= 512 * 1024 * 1024);
    std::vector<u8> bytes(input.size());
    verify(input.read_at(0, bytes.data(), bytes.size()) == bytes.size());
    ppu_exec_object metadata(fs::make_stream(std::vector<u8>(bytes)), 0, elf_opt::no_data);
    std::cout << "Input metadata: sections=" << metadata.shdrs.size() << ", segments=" << metadata.progs.size() << std::endl;
    for (const auto& section : metadata.shdrs)
        std::cout << "Section type=" << static_cast<u32>(static_cast<sec_type>(section.sh_type)) << " address=" << +section.sh_addr << " size=" << +section.sh_size << " flags=" << +section._sh_flags << std::endl;
    rpcs3::ios::aot::instruction_inventory inventory;
    const auto images = rpcs3::ios::aot::analyze_elf(bytes, inventory);
    u32 ppu = 0, spu = 0;
    for (const auto& [unit, count] : inventory) (unit.guest_kind == 1 ? ppu : spu)++;
    std::cout << "Native local executable analysis: PPU images=" << images.ppu_images << ", SPU images=" << images.spu_images
        << ", PPU units=" << ppu << ", SPU units=" << spu << std::endl;
    verify(images.ppu_images + images.spu_images > 0 && !inventory.empty());
}

void test_aot_native_analysis_after_stop()
{
    verify(!g_fxo->is_init());
    const auto spu = image<u32, elf_machine::spu>(0x100, {0x40800003, 0x40800004, 0x32000000});
    rpcs3::ios::aot::instruction_inventory inventory;
    const auto result = rpcs3::ios::aot::analyze_elf(spu, inventory);
    verify(result.spu_images == 1 && !inventory.empty());
    verify(!g_fxo->is_init());
    rpcs3::ios::aot::clear_runtime_spu_programs();
    std::vector<be_t<u32>> local_store(65536);
    for (u32 i = 0; i < 128; ++i) local_store[0x100 / 4 + i] = spu_instructions::AI(7, 7, 1);
    local_store[0x100 / 4 + 128] = 0x35000000; // bi lr
    const std::array<u32, 2> prefix{+local_store[0x100 / 4], +local_store[0x100 / 4 + 1]};
    rpcs3::ios::aot::observe_spu_program(local_store.data(), local_store.data(), 0x100, prefix);
    rpcs3::ios::aot::observe_spu_program(local_store.data(), local_store.data(), 0x104, prefix);
    // Unrelated LS data changes must not evict the first observed native
    // entry while all previously observed instruction words remain intact.
    for (u32 version = 1; version <= 100; ++version)
    {
        local_store[0x3ff00 / 4] = version;
        rpcs3::ios::aot::observe_spu_program(local_store.data(), local_store.data(), 0x104, prefix);
    }
    rpcs3::ios::aot::observe_spu_program(local_store.data(), local_store.data(), 0x100 + 80 * 4, prefix);
    const u32 changed = spu_instructions::AI(7, 7, 2);
    local_store[0x100 / 4 + 80] = changed;
    rpcs3::ios::aot::observe_spu_program(local_store.data(), local_store.data(), 0x100, prefix);
    std::fill(local_store.begin(), local_store.end(), be_t<u32>{0}); // analysis must own its bytes
    inventory.clear();
    rpcs3::ios::aot::collect_runtime_spu_programs(inventory);
    verify(std::any_of(inventory.begin(), inventory.end(), [](const auto& item)
        { return item.first.guest_kind == 2 && item.first.words.size() > 32 && !item.first.blocks.empty(); }));
    verify(std::any_of(inventory.begin(), inventory.end(), [&](const auto& item)
        { return std::find(item.first.words.begin(), item.first.words.end(), changed) != item.first.words.end(); }));
    verify(std::any_of(inventory.begin(), inventory.end(), [&](const auto& item)
        { return item.first.address == 0x100 && item.first.words.size() > 32 &&
            std::find(item.first.words.begin(), item.first.words.end(), changed) == item.first.words.end(); }));
    inventory.clear(); rpcs3::ios::aot::collect_runtime_spu_programs(inventory);
    verify(inventory.empty() && !g_fxo->is_init());

    // A full PPU observation queue must not suppress the independent SPU
    // snapshot owner, or an AOT-only installation can never learn programs.
    namespace aot = rpcs3::ios::aot;
    aot::select_entries({});
    std::vector<aot::instruction> full;
    for (u32 i = 0; i < 65536; ++i) full.push_back({1, i, {i}});
    aot::restore_discoveries(full);
    verify(aot::statistics().pending == 65536);
    for (u32 i = 0; i < 128; ++i) local_store[0x100 / 4 + i] = spu_instructions::AI(7, 7, 1);
    local_store[0x100 / 4 + 128] = 0x35000000;
    rpcs3_aot_runtime_context_v2 runtime{};
    runtime.structure_size = sizeof(runtime); runtime.version = RPCS3_AOT_RUNTIME_VERSION;
    runtime.cpu = local_store.data(); runtime.local_store = local_store.data(); runtime.guest_pc = 0x100;
    runtime.matches_code = [](const auto* context, const u32* words, u32 count)
    {
        if (context->guest_pc >= 0x40000 || count > (0x40000 - context->guest_pc) / 4) return false;
        const auto* at = static_cast<const be_t<u32>*>(context->local_store) + context->guest_pc / 4;
        return std::equal(words, words + count, at, [](u32 a, be_t<u32> b) { return a == +b; });
    };
    runtime.read_code = [](const auto* context, u32* words, u32 capacity) -> u32
    {
        const auto count = std::min(capacity, (0x40000 - context->guest_pc) / 4);
        const auto* at = static_cast<const be_t<u32>*>(context->local_store) + context->guest_pc / 4;
        for (u32 i = 0; i < count; ++i) words[i] = at[i];
        return count;
    };
    verify(!aot::execute(2, +local_store[0x100 / 4], runtime));
    aot::collect_runtime_spu_programs(inventory);
    verify(std::any_of(inventory.begin(), inventory.end(), [](const auto& item)
        { return item.first.guest_kind == 2 && item.first.words.size() > 32; }));
    aot::select_entries({});
    std::cout << "Runtime SPU discovery: full PPU queue and unchanged-prefix/later-code changes preserve native snapshots\n";
    std::cout << "Runtime SPU discovery: owned LS snapshots promote native programs after Stop without JIT or FXO recreation\n";
    std::cout << "Stopped native SPU analysis: succeeds with cleared FXO and does not recreate it\n";
}

void test_aot_native_spu_cache(const std::string& output)
{
    namespace path = std::filesystem;
    const auto root = path::path(output) / "native-spu-cache";
    path::create_directories(root / "ppu-fixture");
    const auto file = root / "ppu-fixture/spu-safe-v1-tane.dat";
    // Use the native cache writer, byte order and CRC rather than reproducing
    // its format in the AOT fixture or implementation.
    {
        spu_cache cache{file.string()};
        for (const u32 address : {0x100u, 0x200u, 0x300u})
        {
            spu_program program{address, address, {}};
            for (const u32 op : {spu_instructions::ILA(4, 0), spu_instructions::AI(4, 4, 1), 0x35000000u})
                program.data.push_back(std::bit_cast<u32>(be_t<u32>{op}));
            cache.add(program);
        }
    }
    {
        fs::file damaged{file.string(), fs::read + fs::write};
        u8 byte = 0; verify(damaged.read_at(damaged.size() - 1, &byte, 1) == 1); byte ^= 1;
        verify(damaged.seek(damaged.size() - 1) == damaged.size() - 1);
        verify(damaged.write(&byte, 1) == 1);
    }
    const auto original = fs::file{file.string(), fs::read}.to_string();
    spu_cache reader{fs::file{file.string(), fs::read}};
    const auto programs = reader.get(); verify(programs.size() == 2 && programs[0].entry_point == 0x200 && programs[1].entry_point == 0x100);
    std::size_t streamed = 0;
    reader.for_each([&](spu_program&& program)
    {
        verify(program.entry_point == (streamed ? 0x200u : 0x100u)); ++streamed; return true;
    });
    verify(streamed == 2);
    streamed = 0; reader.for_each([&](spu_program&&) { ++streamed; return false; }); verify(streamed == 1);
    rpcs3::ios::aot::instruction_inventory inventory;
    rpcs3::ios::aot::collect_cached_spu_programs(root.string(), inventory);
    verify(inventory.size() == 2 && std::all_of(inventory.begin(), inventory.end(), [](const auto& item)
        { return item.first.guest_kind == 2 && !item.first.blocks.empty(); }));
    bool cancelled = false; unsigned checkpoints = 0;
    try { rpcs3::ios::aot::collect_cached_spu_programs(root.string(), inventory, [&] { if (++checkpoints >= 3) throw std::runtime_error("cancel"); }); }
    catch (const std::runtime_error&) { cancelled = true; }
    verify(cancelled && fs::file{file.string(), fs::read}.to_string() == original);
    std::cout << "Native SPU cache reuse: native writer/parser/CRC, streaming, read-only preservation and cancellation passed\n";
}
