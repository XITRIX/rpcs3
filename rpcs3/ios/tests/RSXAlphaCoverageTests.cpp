#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

using u32 = std::uint32_t;
#include "flags.inc"
constexpr u32 NV4097_SET_ANTI_ALIASING_CONTROL = 0;
namespace rsx
{
enum class surface_antialiasing { center_1_sample, multiple_samples };
constexpr u32 pipeline_config_dirty = 1, fragment_program_state_dirty = 2;
}
struct backend_t { bool supports_hw_a2c, supports_hw_a2c_1spp; };
struct decoded_t
{
    u32 bits;
    bool msaa_alpha_to_coverage() const { return bits & 0x10; }
    bool msaa_alpha_to_one() const { return bits & 0x100; }
};
struct registers_t
{
    u32 latch = 0;
    decoded_t value{0};
    rsx::surface_antialiasing samples = rsx::surface_antialiasing::center_1_sample;
    bool msaa_alpha_to_coverage_enabled() const { return value.msaa_alpha_to_coverage(); }
    bool msaa_alpha_to_one_enabled() const { return value.msaa_alpha_to_one(); }
    auto surface_antialias() const { return samples; }
    template<u32> decoded_t decode(u32 bits) const { return {bits}; }
};
struct context
{
    registers_t regs;
    backend_t backend_config{};
    u32 m_graphics_state = 0;
    const backend_t& get_backend_config() const { return backend_config; }
};
#define REGS(ctx) (&(ctx)->regs)
#define RSX(ctx) (ctx)
#include "invalidation.inc"
u32 select(context* m_ctx)
{
    const auto& backend_config = m_ctx->backend_config;
    struct { u32 ctrl = 0x80; } current_fragment_program;
#include "selection.inc"
    return current_fragment_program.ctrl;
}

struct vec4
{
    float r, g, b, a;
    explicit vec4(float v) : r(v), g(v), b(v), a(v) {}
    vec4(float r, float g, float b, float a) : r(r), g(g), b(b), a(a) {}
    vec4() = default;
};
struct discarded {};
float threshold;
bool coverage_test_passes(const vec4& value) { return value.a > threshold; }
#define _mrt_color_t(expr) expr
#define discard throw discarded{}
#define _ENABLE_ALPHA_TO_COVERAGE_TEST
#define _MRT_BUFFERS_COUNT 1
#define MRT0_DO(...) __VA_ARGS__
#define MRT1_DO(...)
#define MRT2_DO(...)
#define MRT3_DO(...)
vec4 coverage_only(vec4 col0)
{
    vec4 ocol0{};
#include "rop.inc"
    return ocol0;
}
#define _ENABLE_ALPHA_TO_ONE
vec4 coverage_and_one(vec4 col0)
{
    vec4 ocol0{};
#include "rop.inc"
    return ocol0;
}
#undef _ENABLE_ALPHA_TO_COVERAGE_TEST
vec4 one_without_coverage(vec4 col0)
{
    vec4 ocol0{};
#include "rop.inc"
    return ocol0;
}

int failures = 0;
void check(bool value, const char* message)
{
    if (!value) { ++failures; std::fprintf(stderr, "%s\n", message); }
}
int main()
{
    for (bool hw : {false, true}) for (bool single_hw : {false, true})
    for (bool multisample : {false, true}) for (bool coverage : {false, true})
    for (bool one : {false, true})
    {
        context ctx;
        ctx.backend_config = {hw, single_hw};
        ctx.regs.samples = multisample ? rsx::surface_antialiasing::multiple_samples : rsx::surface_antialiasing::center_1_sample;
        ctx.regs.value.bits = (coverage ? 0x10 : 0) | (one ? 0x100 : 0);
        const bool emulated = coverage && (!hw || (!multisample && !single_hw));
        const u32 expected = 0x80 | (emulated ? RSX_SHADER_CONTROL_ALPHA_TO_COVERAGE : 0)
            | (emulated && one ? RSX_SHADER_CONTROL_ALPHA_TO_ONE : 0);
        check(select(&ctx) == expected, "program selection / existing control preservation");

        for (u32 old : {0u, 0x10u, 0x100u, 0x110u})
        {
            ctx.regs.latch = old;
            ctx.m_graphics_state = 0;
            set_aa_control(&ctx, 0, ctx.regs.value.bits);
            const bool changed = old != ctx.regs.value.bits;
            const bool shader_changed = changed && !(hw && single_hw)
                && (((old ^ ctx.regs.value.bits) & 0x10) || (coverage && ((old ^ ctx.regs.value.bits) & 0x100)));
            check(ctx.m_graphics_state == ((changed ? 1u : 0u) | (shader_changed ? 2u : 0u)), "AA state shader invalidation");
        }
    }

    for (float alpha : {0.f, 1.f / 255.f, .49f, .5f, .75f, 1.f})
    for (float cutoff : {0.f, .001f, .1f, .5f, .9f})
    {
        threshold = cutoff;
        const vec4 sample{108.f / 255.f, 117.f / 255.f, 65.f / 255.f, alpha};
        for (bool one : {false, true})
        {
            bool survived = false;
            try
            {
                const auto result = one ? coverage_and_one(sample) : coverage_only(sample);
                survived = true;
                check(result.a == (one ? 1.f : alpha), "surviving output alpha");
                check(result.r == sample.r && result.g == sample.g && result.b == sample.b, "RGB preserved");
                if (one) check(std::max(result.g / std::max(result.a, 1.f / 255.f) - 8.f, 0.f) == 0.f, "captured tree pixel no false bloom amplification");
            }
            catch (const discarded&) {}
            check(survived == (alpha > cutoff), "coverage must use original alpha before alpha-to-one");
        }
        check(one_without_coverage(sample).a == 1.f, "epilogue alpha-to-one without coverage test");
    }
    if (failures) { std::fprintf(stderr, "%d failures\n", failures); return 1; }
    std::puts("A2C program selection, state invalidation, original-alpha coverage, RGB preservation and HDR amplification checks pass.");
}
