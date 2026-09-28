#include <algorithm>
#include <cassert>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
struct position2u { u32 x, y; };
struct size2u { u32 width, height; };
struct image_section_attributes_t { u16 width, height, mipmaps, slice_h; };
namespace surface_transform { constexpr u32 coordinate_transform = 1; }
struct region
{
    int src;
    u32 xform;
    u32 base_addr;
    u8 level;
    u16 src_x, src_y, dst_x, dst_y, dst_z, src_w, src_h, dst_w, dst_h;
};
#include "CubemapBorderUnderTest.inc"

int main()
{
    // GT6's captured 256x256, nine-mip cube occupies 529 rows per face:
    // 511 image rows plus a top and bottom border for every mip.
    const image_section_attributes_t gt6{256, 256, 9, 529};
    const u32 pitch = 320, rows = 529 * 6;
    std::vector<int> atlas(pitch * rows, -1);
    for (u32 face = 0; face < 6; ++face)
    {
        u32 row = face * 529;
        for (u32 mip = 0, side = 256; mip < 9; ++mip, side = std::max(side / 2, 1u))
        {
            for (u32 y = 0; y < side; ++y)
                for (u32 x = 0; x < side; ++x)
                    atlas[(row + y + 1) * pitch + x + 1] = static_cast<int>(face * 9 + mip);
            row += side + 2;
        }
        assert(row == (face + 1) * 529);
    }
    auto identity = [](u16 x, u16 y) { return std::pair{x, y}; };
    std::vector<region> regions;
    append_bordered_cubemap_sections(regions, 42, gt6, {0, 0}, {256, 256}, identity);
    assert(regions.size() == 54);
    for (const auto& r : regions)
    {
        assert(r.src == 42 && r.xform == surface_transform::coordinate_transform);
        assert(r.src_w == r.dst_w && r.src_h == r.dst_h && !r.dst_x && !r.dst_y);
        for (u32 y = 0; y < r.src_h; ++y)
            for (u32 x = 0; x < r.src_w; ++x)
                assert(atlas[(r.src_y + y) * pitch + r.src_x + x] == r.dst_z * 9 + r.level);
    }
    // Exact independently-derived mip starts; the old unwrap used 0,256,384,...
    const u16 expected_y[]{1,259,389,455,489,507,517,523,527};
    for (u32 i = 0; i < regions.size(); ++i)
        assert(regions[i].src_x == 1 && regions[i].src_y == (i / 9) * 529 + expected_y[i % 9]);

    // Fractional resolution scaling must transform complete coordinates, not
    // accumulate separately rounded border sizes. Destinations follow the
    // host mip chain, including its one-texel tail.
    for (u32 percent : {50, 75, 100, 150, 200})
    {
        auto scale = [=](u16 x, u16 y) { return std::pair{u16(x * percent / 100), u16(y * percent / 100)}; };
        std::vector<region> scaled;
        const u16 host_side = 256 * percent / 100;
        append_bordered_cubemap_sections(scaled, 7, gt6, {13, 17}, {host_side, host_side}, scale);
        const u32 host_levels = std::min<u32>(9, std::bit_width(host_side));
        assert(scaled.size() == 6 * host_levels);
        for (u32 i = 0; i < scaled.size(); ++i)
        {
            const auto& r = scaled[i];
            const u32 face = i / host_levels, level = i % host_levels;
            const u32 y = 17 + face * 529 + expected_y[level];
            const u32 side = 256u >> level;
            assert(r.src_x == 14 * percent / 100 && r.src_y == y * percent / 100);
            assert(r.src_w == std::max(((14 + side) * percent / 100) - (14 * percent / 100), 1u));
            assert(r.src_h == std::max(((y + side) * percent / 100) - (y * percent / 100), 1u));
            assert(r.dst_w == std::max<u32>(host_side >> level, 1));
            assert(r.dst_h == r.dst_w && r.dst_z == face && r.level == level);
        }
    }
    // A single-mip cube, subregion offset and a non-square texture exercise
    // dimensions independently of the GT6 fixture. Preserve existing sections.
    const image_section_attributes_t small{8, 4, 1, 6};
    std::vector<region> single(1);
    append_bordered_cubemap_sections(single, 9, small, {5, 7}, {8, 4}, identity);
    assert(single.size() == 7);
    for (u32 face = 0; face < 6; ++face)
    {
        const auto& r = single[face + 1];
        assert(r.src_x == 6 && r.src_y == 8 + face * 6 && r.src_w == 8 && r.src_h == 4);
        assert(r.dst_z == face && r.level == 0);
    }
    std::puts("Cubemap border: 54 sentinel-free face/mip copies, five scale factors and offset/single-mip checks passed");
}
