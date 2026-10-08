// Reuse the production extraction harness and its immutable pre-fix control.
#define main minecraft_differential_main
#include "MinecraftVertexTests.cpp"
#undef main
#include <sys/mman.h>
#include <unistd.h>

template <typename Range>
void check_ranges(bool check_captured_range = true)
{
    method_registers = {};
    renderer.local_mem_size = 0xf900000;

    // Captured BLUS31509: 290 vertices, four per-instance attributes sharing
    // a 68-byte record at 0xcf8fff80. Integer division fetches only record 0.
    Range captured;
    captured.attribute_stride = 68;
    captured.real_offset_address = 0xcf8fff80;
    for (u8 index : {1, 4, 5, 6})
        captured.locations.push_back({index, false, 290});
    auto range = captured.calculate_required_range(0, 290);
    if (check_captured_range)
    {
        assert(range == std::make_pair(0u, 1u));
        assert(captured.calculate_required_range(1, 592) == range); // Cached result.
    }

    // An actual guarded read at the captured boundary, not only arithmetic.
    const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    auto* allocation = static_cast<std::byte*>(mmap(nullptr, page * 2,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
    assert(allocation != MAP_FAILED);
    assert(mprotect(allocation + page, page, PROT_NONE) == 0);
    std::memset(allocation + page - 128, 0x5a, 128);
    std::vector<std::byte> output(range.second * captured.attribute_stride);
    std::memcpy(output.data(), allocation + page - 128, output.size());
    for (auto value : output) assert(value == std::byte{0x5a});
    assert(munmap(allocation, page * 2) == 0);

    // Independently enumerate shader fetches instead of comparing to the buggy
    // baseline. Covers mixed divisors, modulo, zero/unit frequency and nonzero
    // first vertices. Array-mode modulo bounds may conservatively start at 0.
    std::mt19937 random(0x31509);
    for (u32 n = 0; n < 100000; ++n)
    {
        Range info;
        info.attribute_stride = 16;
        info.real_offset_address = 0xc0000000;
        const u32 first = random() % 128;
        const u32 count = 1 + random() % 256;
        u32 maximum = 0, minimum = first;
        for (u8 index = 0, end = 1 + random() % 8; index < end; ++index)
        {
            const u16 frequency = random() % 320;
            const bool modulo = random() % 2;
            info.locations.push_back({index, modulo, frequency});
            if (frequency <= 1) maximum = std::max(maximum, first + count - 1);
            else if (modulo)
            {
                if (first + count - 1 >= frequency)
                {
                    minimum = 0;
                    maximum = std::max(maximum, u32(frequency - 1));
                }
                for (u32 vertex = first; vertex < first + count; ++vertex)
                    maximum = std::max(maximum, vertex % frequency);
            }
            else
            {
                minimum = std::min(minimum, first / frequency);
                for (u32 vertex = first; vertex < first + count; ++vertex)
                    maximum = std::max(maximum, vertex / frequency);
            }
        }
        assert(info.calculate_required_range(first, count) ==
            std::make_pair(minimum, maximum - minimum + 1));
    }

    // Indexed modulo tightening near local/main boundaries, with division
    // before/after modulo. A rounded-up divided index wrongly prunes frequency
    // 2 and bypasses re-evaluation, yielding three records instead of two.
    for (bool local : {false, true})
    for (bool reverse : {false, true})
    for (bool restart : {false, true})
    for (bool wide : {false, true})
    for (bool immediate : {false, true})
    {
        method_registers = {};
        method_registers.current_draw_clause.command = draw_command::indexed;
        method_registers.current_draw_clause.is_immediate_draw = immediate;
        method_registers.it = wide ? index_array_type::u32 : index_array_type::u16;
        method_registers.restart = restart;
        method_registers.restart_value = wide || immediate ? ~0u : 65535;
        push_indices.clear();
        const bool use_u32 = wide || immediate;
        for (unsigned index = 0; index < 6; ++index)
        {
            const u32 value = restart && index % 2 ? method_registers.restart_value : 0;
            if (immediate) push_indices.push_back(value);
            else if (use_u32)
            {
                const be_t<u32> encoded = value;
                std::memcpy(vm::memory.data() + index * 4, &encoded, 4);
            }
            else
            {
                const be_t<u16> encoded = static_cast<u16>(value);
                std::memcpy(vm::memory.data() + index * 2, &encoded, 2);
            }
        }
        Range info;
        info.attribute_stride = 68;
        info.real_offset_address = local ? 0xcf8fff80 : 0xffff80;
        const interleaved_attribute_t divided{1, false, 4}, modulo{2, true, 2};
        info.locations.push_back(reverse ? modulo : divided);
        info.locations.push_back(reverse ? divided : modulo);
        assert(info.calculate_required_range(0, 6) == std::make_pair(0u, 2u));
        // Division needs records 0 and 1, even when all modulo fetches are 0.
    }
    push_indices.clear();
    method_registers = {};
    Range single;
    single.single_vertex = true;
    assert(single.calculate_required_range(100, 290) == std::make_pair(0u, 1u));
}

int main(int argc, char** argv)
{
    if (argc > 1 && std::string(argv[1]) == "original-copy")
        check_ranges<old::interleaved_range_info>(false);
    else if (argc > 1 && std::string(argv[1]) == "original")
        check_ranges<old::interleaved_range_info>();
    else
        check_ranges<candidate::interleaved_range_info>();
    std::puts("PASS: WRC4 captured boundary/guarded copy; 100000 shader-semantic ranges; indexed/restart/immediate/mixed-frequency controls");
}
