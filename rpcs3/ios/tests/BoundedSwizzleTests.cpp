#include "Emu/RSX/Utils/bounded_swizzle.hpp"

#include <cassert>
#include <iostream>
#include <vector>

// Independent Morton oracle: interleave the low coordinate bits, then concatenate
// the remaining X/Y tiles. It does not use the production carry-mask recurrence.
static std::uint64_t morton(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height)
{
    const auto bits = std::min(std::bit_width(width - 1), std::bit_width(height - 1));
    std::uint64_t result = (std::uint64_t{x >> bits} + (std::uint64_t{y >> bits})) << (2 * bits);
    for (int bit = 0; bit < bits; ++bit)
    {
        result |= std::uint64_t{(x >> bit) & 1} << (2 * bit);
        result |= std::uint64_t{(y >> bit) & 1} << (2 * bit + 1);
    }
    return result;
}

template <typename T>
static void check(std::uint16_t width, std::uint16_t height, std::uint32_t pitch, std::size_t source_size, std::size_t dest_size)
{
    std::vector<std::uint8_t> source(source_size);
    for (std::size_t i = 0; i < source.size(); ++i) source[i] = (i * 73 + 19) % 251;
    const auto original = source;
    std::vector<std::uint8_t> guarded(dest_size + 32, 0xd7);
    std::vector<std::uint8_t> expected = guarded;
    for (std::uint32_t y = 0; y < height; ++y)
    {
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const auto dst = morton(x, y, width, height) * sizeof(T);
            const auto src = (std::uint64_t{y} * (pitch / sizeof(T)) + x) * sizeof(T);
            for (unsigned byte = 0; byte < sizeof(T) && dst + byte < dest_size; ++byte)
            {
                expected[16 + dst + byte] = src + byte < source.size() ? source[src + byte] : 0;
            }
        }
    }
    rsx::convert_linear_swizzle_bounded<T>(source, {guarded.data() + 16, dest_size}, width, height, pitch);
    assert(guarded == expected);
    assert(source == original);
}

int main()
{
    std::uint64_t cases = 0;
    for (std::uint16_t width = 1; width <= 35; ++width)
    {
        for (std::uint16_t height = 1; height <= 35; ++height)
        {
            for (const unsigned stride : {0u, 1u, unsigned(width - 1), unsigned(width), unsigned(width + 3)})
            {
                const std::size_t bytes = stride * height * 4;
                for (const auto source_size : {std::size_t{0}, bytes / 2 + 1, bytes})
                {
                    for (const auto dest_size : {std::size_t{0}, bytes / 3 + 1, bytes})
                    {
                        check<std::uint16_t>(width, height, stride * 2, source_size, dest_size);
                        check<std::uint32_t>(width, height, stride * 4, source_size, dest_size);
                        cases += 2;
                    }
                }
            }
        }
    }
    // Wide pitch arithmetic, non-square extents and partial final texels.
    check<std::uint32_t>(3, 7, UINT32_MAX, 31, 127);
    check<std::uint16_t>(65535, 1, 131070, 1024, 2049);
    check<std::uint32_t>(1, 65535, 4, 1024, 2049);
    check<std::uint32_t>(1366, 768, 1366 * 4, 1366 * 768 * 4, 1366 * 768 * 4);
    check<std::uint16_t>(0, 32, 0, 0, 64);
    check<std::uint32_t>(32, 0, 128, 128, 64);
    std::cout << "Bounded swizzle: " << cases + 6 << " oracle cases passed\n";
}
