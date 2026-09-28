#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace rsx::texture_cache_helpers
{
#include "FramebufferSourceUnderTest.inc"
}

struct surface
{
    std::uint64_t last_use_tag;
};
struct overlap
{
    const struct surface* surface;
    bool is_depth;
    bool is_clipped;
    int id;
};

int main()
{
    const surface old{41}, current{42}, newer{43};
    const overlap color{&current, false, false, 1};
    const overlap depth{&current, true, false, 2};
    int checks = 0;
    auto expect = [&](std::vector<overlap> candidates, bool depth_texture, int id)
    {
        assert(rsx::texture_cache_helpers::select_framebuffer_source(candidates, depth_texture).id == id);
        ++checks;
    };

    // GT6 mirror: complete color and oversized depth allocations share a write
    // tag. Input order must not force a color texture to sample the depth image.
    expect({color, depth}, false, 1);
    expect({depth, color}, false, 1);
    expect({color, depth}, true, 2);
    expect({depth, color}, true, 2);

    // A later write owns the data, even when a typeless conversion is required.
    expect({color, {&newer, true, false, 3}}, false, 3);
    expect({depth, {&newer, false, false, 3}}, true, 3);
    expect({{&old, false, false, 3}, depth}, false, 2);
    expect({{&old, true, false, 3}, color}, true, 1);

    // No matching aspect: retain depth/color reinterpretation and single hits.
    expect({depth}, false, 2);
    expect({color}, true, 1);
    expect({color}, false, 1);
    expect({depth}, true, 2);

    // Partial coverage must still reach the merge path, and must never replace
    // a complete source just to match the requested aspect.
    expect({color, {&newer, true, true, 3}}, false, 3);
    expect({depth, {&newer, false, true, 3}}, true, 3);
    expect({{&current, false, true, 3}, depth}, false, 2);
    expect({{&current, true, true, 3}, color}, true, 1);
    expect({{&current, false, true, 3}, {&current, true, true, 4}}, false, 4);

    // Retain existing ordering within the matching aspect; only inspect the
    // newest timestamp group of the already sorted overlap list.
    expect({{&old, false, false, 3}, color, depth}, false, 1);
    expect({color, {&current, false, false, 3}, depth}, false, 3);
    expect({depth, {&current, true, false, 3}, color}, true, 3);
    expect({color, {&current, false, false, 3}}, false, 3);
    std::printf("Framebuffer source selection: %d checks passed\n", checks);
}
