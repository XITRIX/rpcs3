#include "Emu/Memory/vm_reservation_layout.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>

int main()
{
    constexpr unsigned stride = vm::reservation_stride;
    static_assert(stride == 64 || stride == 128);
    static_assert(vm::reservation_table_size == stride * 512);
    std::array<bool, 512> seen{};
    for (std::uint32_t address = 0; address < 65536; ++address)
    {
        const auto offset = vm::reservation_offset(address);
        const auto index = (address & 0xff80) / 128;
        assert(offset == index * stride);
        assert(offset % stride == 0);
        assert(offset + 32 <= vm::reservation_table_size);
        // Generated SPU address arithmetic must address the native stamp.
        assert(offset == ((address & 0xff80) >> vm::reservation_stride_shift));
        for (const std::uint32_t high : {0u, 0x10000u, 0x7fff0000u, 0xffff0000u})
            assert(vm::reservation_offset(address | high) == offset);
        seen[index] = true;
    }
    for (bool slot : seen) assert(slot);
    // Preserve exactly the original guest alias equivalence relation.
    for (unsigned a = 0; a < 512; ++a)
        for (unsigned b = 0; b < 512; ++b)
            assert((vm::reservation_offset(a * 128) == vm::reservation_offset(b * 128)) == (a == b));
#if defined(__APPLE__) && defined(__aarch64__)
    static_assert(stride == 128);
    for (unsigned a = 1; a < 512; ++a)
        assert(vm::reservation_offset(a * 128) / 128 != vm::reservation_offset((a-1) * 128) / 128);
#else
    static_assert(stride == 64);
#endif
    std::printf("reservation layout: all 65,536 low addresses, high-address aliases, 262,144 slot pairs and bounds passed; stride=%u\n", stride);
}
