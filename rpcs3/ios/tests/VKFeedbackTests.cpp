#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s32 = int32_t;

static void require(bool condition, const char* message)
{
	if (!condition) throw std::runtime_error(message);
}

union v128
{
	u32 _u32[4];
	u64 _u64[2];
	static v128 loadu(const void* data, s32 index)
	{
		v128 result;
		std::memcpy(&result, static_cast<const u8*>(data) + index * 16, 16);
		return result;
	}
};

#include "FragmentAnalysis.inc"

constexpr u32 VK_IMAGE_ASPECT_COLOR_BIT = 1;
constexpr u32 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL = 6;
struct coord3u { u32 x, y, z, w, h, d; };
namespace rsx
{
	enum class surface_access { transfer_read };
	enum class surface_transform { identity };
	struct image_section_attributes_t { u32 address{}, gcm_format{}, width{}, height{}, depth{}; };
	struct fake_texture
	{
		u32 remap{};
		u32 decoded_remap() const { return remap; }
	};
	struct { std::array<fake_texture, 16> fragment_textures; } method_registers;
}
namespace vk
{
	enum class driver_vendor { MVK, OTHER };
	driver_vendor driver = driver_vendor::MVK;
	driver_vendor get_driver_vendor() { return driver; }
	struct command_buffer {};
	struct image_view;
	struct image
	{
		u32 w = 1920, h = 1080, sample_count = 1, spp = 1, range = 0x4000;
		u32 content = 1, barriers = 0, transitions = 0, layout_depth = 0;
		u32 selected_remap = 0;
		image_view* view{};
		image_view* get_view(u32 remap) { selected_remap = remap; return view; }
		u32 samples() const { return sample_count; }
		u32 width() const { return w; }
		u32 height() const { return h; }
		u32 get_memory_range() const { return range; }
		void memory_barrier(command_buffer&, rsx::surface_access) { ++barriers; }
		void push_layout(command_buffer&, u32 layout)
		{
			require(layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, "Missing attachment-to-transfer barrier");
			++transitions;
			++layout_depth;
		}
		void pop_layout(command_buffer&) { require(layout_depth == 1, "Unbalanced layout stack"); --layout_depth; }
	};
	using viewable_image = image;
	struct image_view
	{
		explicit image_view(vk::image* image) : resource(image) { image->view = this; }
		vk::image* resource{};
		struct { struct { u32 aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; } subresourceRange; } info;
		u32 fmt = 97;
		vk::image* image() const { return resource; }
		u32 format() const { return fmt; }
		image_view* as(u32 format) { fmt = format; return this; }
	};
	struct texture_cache
	{
		struct sampled_image_descriptor
		{
			u32 ref_address = 0x2000;
			struct { u32 format() const { return 0xba; } } format_ex;
			image_view* image_handle{};
		};
		struct deferred_subresource : rsx::image_section_attributes_t
		{
			image* source{};
			coord3u region{};
			u32 remap{}, cache_range{};
			bool dynamic{};
			static deferred_subresource create_copy(image* source, const rsx::image_section_attributes_t& attrs,
				const coord3u& rect, rsx::surface_transform, u32 remap, bool dynamic)
			{
				deferred_subresource op;
				static_cast<rsx::image_section_attributes_t&>(op) = attrs;
				op.source = source; op.region = rect; op.remap = remap; op.dynamic = dynamic;
				return op;
			}
		};
		image storage;
		image_view snapshot{&storage};
		deferred_subresource last;
		u32 calls = 0;
		bool fail = false;
		image_view* create_temporary_subresource(command_buffer&, deferred_subresource& copy)
		{
			require(copy.source->barriers != 0 && copy.source->layout_depth == 1, "Copy before source barriers");
			last = copy;
			++calls;
			if (fail) return nullptr;
			if (copy.dynamic || calls == 1) storage.content = copy.source->content;
			return &snapshot;
		}
	};
}

struct VKGSRender
{
	fragment_program_utils::fragment_program_metadata current_fp_metadata{};
	struct { std::array<std::pair<u32, vk::image*>, 4> m_bound_render_targets{}; } m_rtts;
	std::array<std::unique_ptr<vk::texture_cache::sampled_image_descriptor>, 16> fs_sampler_state;
	vk::command_buffer command;
	vk::command_buffer* m_current_command_buffer = &command;
	vk::texture_cache m_texture_cache;
	vk::image_view* snapshot_color_feedback(u32 texture_index, vk::image_view* view);
};
#include "VKFeedback.inc"

static v128 instruction(u32 opcode, u32 texture = 0, bool end = false, bool constant = false)
{
	// Encode directly from ISA bit positions, independently of OPDEST's decoder.
	const u32 dest = (opcode << 24) | (texture << 17) | u32(end);
	v128 value{};
	value._u32[0] = ((dest & 0xff00ff) << 8) | ((dest & 0xff00ff00) >> 8);
	value._u32[1] = constant ? 0x200 : 0;
	return value;
}

static auto analyze(std::initializer_list<v128> instructions)
{
	return fragment_program_utils::analyse_fragment_program(instructions.begin());
}

static void test_analysis()
{
	const auto empty = analyze({instruction(0, 0, true)});
	require(empty.is_nop_shader && empty.multiple_texture_reads_mask == 0, "NOP shader samples textures");
	const auto single = analyze({instruction(0x17, 2, true)});
	require(single.referenced_textures_mask == 4 && single.multiple_texture_reads_mask == 0, "Single-read shader selected");
	const auto mixed = analyze({instruction(0x17, 2), instruction(0x17, 8), instruction(0x17, 2, true)});
	require(mixed.multiple_texture_reads_mask == 4 && mixed.referenced_textures_mask == 0x104, "Repeated texture unit not identified");
	for (u32 op : {0x17u, 0x18u, 0x19u, 0x2fu, 0x31u, 0x33u, 0x34u})
	{
		for (u32 unit = 0; unit < 16; ++unit)
		{
			const auto result = analyze({instruction(0), instruction(op, unit), instruction(op, unit, true)});
			require(result.multiple_texture_reads_mask == (1u << unit), "Sample opcode/unit missed");
			require(result.program_start_offset == 16 && result.program_ucode_length == 32, "Program span changed");
		}
	}
	const auto literal = analyze({instruction(0x17, 3, false, true), instruction(0x17, 3), instruction(1, 0, true)});
	require(literal.multiple_texture_reads_mask == 0 && literal.program_constants_buffer_length == 16,
		"Constant payload misidentified as another texture read");
}

static void test_binding()
{
	VKGSRender renderer;
	vk::image attachment, ordinary;
	vk::image_view view{&attachment}, unrelated{&ordinary};
	const u32 unit = 2;
	renderer.current_fp_metadata.multiple_texture_reads_mask = 1 << unit;
	renderer.m_rtts.m_bound_render_targets[3] = {0x1000, &attachment};
	auto desc = std::make_unique<vk::texture_cache::sampled_image_descriptor>();
	desc->image_handle = &view;
	renderer.fs_sampler_state[unit] = std::move(desc);
	rsx::method_registers.fragment_textures[unit].remap = 0x1234;
	auto& cache = renderer.m_texture_cache;

	vk::driver = vk::driver_vendor::OTHER;
	require(renderer.snapshot_color_feedback(unit, &view) == &view, "Changed non-MoltenVK feedback");
	vk::driver = vk::driver_vendor::MVK;
	require(renderer.snapshot_color_feedback(1, &view) == &view, "Changed single-read feedback");
	require(renderer.snapshot_color_feedback(unit, &unrelated) == &unrelated, "Copied an unbound texture");
	view.info.subresourceRange.aspectMask = 2;
	require(renderer.snapshot_color_feedback(unit, &view) == &view, "Changed depth feedback");
	view.info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	attachment.sample_count = 4;
	require(renderer.snapshot_color_feedback(unit, &view) == &view, "Changed MSAA behavior");
	attachment.sample_count = 1;
	attachment.spp = 4;
	require(renderer.snapshot_color_feedback(unit, &view) == &view, "Changed RSX MSAA with host AA disabled");
	attachment.spp = 1;
	require(cache.calls == 0 && attachment.barriers == 0, "Excluded draws incur transfer work");

	auto first = renderer.snapshot_color_feedback(unit, &view);
	require(first != &view && first->image()->content == 1, "Missing framebuffer snapshot");
	require(cache.last.dynamic && cache.last.width == 1920 && cache.last.height == 1080,
		"Snapshot is stale or uses guest resolution");
	require(cache.last.region.x == 0 && cache.last.region.y == 0 && cache.last.region.w == 1920 && cache.last.region.h == 1080,
		"Cropped full source; existing shader coordinate transform no longer applies");
	require(cache.last.address == 0x2000 && cache.last.cache_range == attachment.range && cache.last.remap == 0x1234,
		"Alias address, lifetime range or channel remap lost");
	require(first->format() == view.format() && attachment.layout_depth == 0, "View format or source layout changed");

	attachment.content = 42;
	rsx::method_registers.fragment_textures[unit].remap = 0x5678;
	auto second = renderer.snapshot_color_feedback(unit, &view);
	require(first == second && second->image()->content == 42 && cache.calls == 2, "Reused snapshot was not refreshed");
	require(second->image()->selected_remap == 0x5678, "Reused snapshot retained an old sampler remap");
	require(renderer.fs_sampler_state[unit]->image_handle == &view, "Cached original descriptor replaced with snapshot");
	require(renderer.snapshot_color_feedback(unit, second) == second && cache.calls == 2, "Already separated/strict-mode view copied again");
	renderer.current_fp_metadata.multiple_texture_reads_mask = 0;
	require(renderer.snapshot_color_feedback(unit, &view) == &view, "Shader transition retained feedback snapshot");
	renderer.current_fp_metadata.multiple_texture_reads_mask = 1 << unit;
	cache.fail = true;
	require(renderer.snapshot_color_feedback(unit, &view) == nullptr && attachment.layout_depth == 0,
		"Allocation failure uses unsafe original view or fails to restore source layout");
}

int main(int argc, char** argv)
{
	try
	{
		test_analysis();
		test_binding();
		std::cout << "Fragment analysis and MoltenVK feedback binding regressions passed\n";
		for (int i = 1; i < argc; ++i)
		{
			std::ifstream file(argv[i], std::ios::binary);
			std::vector<u8> data((std::istreambuf_iterator<char>(file)), {});
			require(data.size() >= 16 && data.size() % 16 == 0, "Invalid captured shader");
			const auto result = fragment_program_utils::analyse_fragment_program(data.data());
			std::cout << argv[i] << ": referenced=" << result.referenced_textures_mask
				<< " repeated=" << result.multiple_texture_reads_mask << '\n';
		}
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
