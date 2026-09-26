#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

using uint = std::uint32_t;
using uvec2 = uint __attribute__((ext_vector_type(2)));
using uvec3 = uint __attribute__((ext_vector_type(3)));

[[maybe_unused]] static uvec2 max(uvec2 a, uvec2 b)
{
    return {std::max(a.x, b.x), std::max(a.y, b.y)};
}

[[maybe_unused]] static uvec3 max(uvec3 a, uvec3 b)
{
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

static uint image_width, image_height, image_depth;
static uint image_logw, image_logh, image_logd, lod_count;

struct invocation_properties
{
    uint data_offset;
    uvec3 size;
    uvec3 size_log2;
};

static invocation_properties invocation;

#include "GPUDeswizzleUnderTest.inc"

static uint checked_texels = 0;
static uint checked_levels = 0;

[[noreturn]] static void fail(const char* reason, uint level, uint offset)
{
    std::fprintf(stderr, "%s: base=%ux%ux%u, lods=%u, level=%u, offset=%u\n",
                 reason, image_width, image_height, image_depth, lod_count, level, offset);
    std::exit(1);
}

// Independent oracle: construct the Morton address by enumerating the bit
// positions of each axis, rather than shifting mutable coordinates.
static uint reference_address(std::array<uint, 3> point, std::array<uint, 3> size)
{
    uint address = 0;
    uint destination_bit = 0;
    for (uint bit = 0; bit < 16; ++bit)
    {
        for (uint axis = 0; axis < 3; ++axis)
        {
            if ((1u << bit) < size[axis])
            {
                address |= ((point[axis] >> bit) & 1u) << destination_bit++;
            }
        }
    }
    return address;
}

static void check(uint width, uint height, uint depth, uint levels)
{
    image_width = width;
    image_height = height;
    image_depth = depth;
    image_logw = std::bit_width(width) - 1;
    image_logh = std::bit_width(height) - 1;
    image_logd = std::bit_width(depth) - 1;
    lod_count = levels;

    uint base = 0;
    for (uint level = 0; level < levels; ++level)
    {
        const std::array<uint, 3> size = {
            std::max(width >> level, 1u),
            std::max(height >> level, 1u),
            std::max(depth >> level, 1u),
        };
        const uint texels = size[0] * size[1] * size[2];
        for (uint i = 0; i < texels; ++i)
        {
            const uint offset = base + i;
            if (!init_invocation_properties(offset))
                fail("valid texel rejected", level, offset);
            if (invocation.data_offset != base)
                fail("wrong mip base", level, offset);
            for (uint axis = 0; axis < 3; ++axis)
            {
                if (invocation.size[axis] != size[axis] ||
                    invocation.size_log2[axis] != static_cast<uint>(std::bit_width(size[axis]) - 1))
                    fail("wrong mip dimensions", level, offset);
            }

            const uint local = offset - invocation.data_offset;
            const uint x = local % invocation.size.x;
            const uint y = local / invocation.size.x % invocation.size.y;
            const uint z = local / (invocation.size.x * invocation.size.y);
            const uint actual = invocation.data_offset + get_z_index(x, y, z);
            const uint expected = base + reference_address({x, y, z}, size);
            if (actual != expected || actual < base || actual >= base + texels)
                fail("wrong or out-of-range source address", level, offset);
            ++checked_texels;
        }
        base += texels;
        ++checked_levels;
    }

    // Dispatch sizes are rounded to workgroups, so surplus invocations must
    // not read or write beyond the final mip.
    for (uint offset = base; offset < base + 1024; ++offset)
    {
        if (init_invocation_properties(offset))
            fail("out-of-range invocation accepted", levels, offset);
    }
}

int main()
{
    // The 64x64x32, three-level volume is used by the captured GTA V skin
    // shader. Include truncated chains and axes reaching one at different
    // levels, plus 1D/2D textures that must preserve their existing layout.
    check(64, 64, 32, 3);
    for (uint w : {1u, 2u, 4u, 8u, 16u, 32u, 64u})
    {
        for (uint h : {1u, 2u, 4u, 8u, 16u, 32u})
        {
            for (uint d : {1u, 2u, 4u, 8u, 16u, 32u})
            {
                const uint max_levels = std::bit_width(std::max({w, h, d}));
                for (uint levels = 1; levels <= max_levels; ++levels)
                    check(w, h, d, levels);
            }
        }
    }
    std::printf("GPU deswizzle: %u mip layouts, %u texel addresses and dispatch bounds passed\n",
                checked_levels, checked_texels);
}
