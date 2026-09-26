#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include "Emu/RSX/Common/sampler_invalidation.h"

using u16 = std::uint16_t;
using u32 = std::uint32_t;
#include "VKScalingFlags.inc"

void require(bool condition, const char* message)
{
	if (!condition) throw std::runtime_error(message);
}

namespace rsx
{
	struct surface_scaling_config_t
	{
		u16 scale_percent = 75;
		u16 min_scalable_dimension = 16;
		bool operator==(const surface_scaling_config_t&) const = default;
	};
	enum class problem_severity { low, moderate, fatal };
	enum class framebuffer_creation_context { context_draw, context_clear };
	enum class texture_upload_context { shader_read, framebuffer_storage };
}

namespace vk
{
	rsx::problem_severity severity = rsx::problem_severity::low;
	auto vmm_determine_memory_load_severity() { return severity; }
	bool renderpass_open = false;
	void end_renderpass(int)
	{
		// queue_swap_request has submitted the old buffer and begun a new one
		// before scale synchronization. Vulkan forbids ending a nonexistent pass.
		require(renderpass_open, "vkCmdEndRenderPass without an active render pass");
		renderpass_open = false;
	}
}

struct
{
	struct { u16 resolution_scale_percent = 75; u16 min_scalable_dimension = 16; } video;
} g_cfg;

struct Flags
{
	u32 bits = 0;
	bool test(u32 value) const { return (bits & value) != 0; }
	void clear(u32 value) { bits &= ~value; }
	void operator|=(u32 value) { bits |= value; }
};

struct Framebuffer
{
	unsigned image = 0;
	int refs = 1;
	bool alive = true;
	void release()
	{
		require(alive && refs == 1, "released a destroyed or unowned framebuffer");
		--refs;
	}
};

struct Sampler { rsx::texture_upload_context upload_context; };

struct SurfaceCache
{
	unsigned image = 1;
	unsigned replacements = 0;
	unsigned pressure_calls = 0;
	std::function<void()> before_reclaim;
	void sync_scaling_config(int, const rsx::surface_scaling_config_t&)
	{
		before_reclaim();
		++image;
		++replacements;
	}
	bool handle_memory_pressure(int, rsx::problem_severity)
	{
		before_reclaim();
		++pressure_calls;
		return true;
	}
};

struct VKGSRender
{
	using Context = rsx::framebuffer_creation_context;
	rsx::surface_scaling_config_t resolution_scaling_config;
	SurfaceCache m_rtts;
	std::vector<std::unique_ptr<Framebuffer>> framebuffers;
	Framebuffer* m_draw_fbo = nullptr;
	std::vector<unsigned> m_fbo_images;
	Flags m_graphics_state;
	Context m_current_framebuffer_context = Context::context_draw;
	struct { bool ignore_change = true; } m_framebuffer_layout;
	std::array<std::unique_ptr<Sampler>, 16> fs_sampler_state;
	std::array<std::unique_ptr<Sampler>, 4> vs_sampler_state;
	std::array<bool, 16> m_textures_dirty{};
	std::array<bool, 4> m_vertex_textures_dirty{};
	std::atomic<bool> m_samplers_dirty{false};
	int command_buffer = 0;
	int* m_current_command_buffer = &command_buffer;
	unsigned rebuilds = 0;
	unsigned viewport_scale = 75;
	unsigned scissor_scale = 75;
	bool valid_layout = true;
	unsigned flushes = 0;

	void sync_config();
	void prepare_rtts(Context);
	void close_render_pass();
	void flush_command_queue(bool) { ++flushes; }
	void set_scissor(bool)
	{
		if (m_graphics_state.test(rsx::scissor_config_state_dirty))
		{
			scissor_scale = resolution_scaling_config.scale_percent;
			m_graphics_state.clear(rsx::scissor_config_state_dirty);
		}
	}
	void get_framebuffer_layout(Context context, decltype(m_framebuffer_layout)& layout)
	{
		// Guest registers are unchanged by the host scaling slider.
		layout.ignore_change = true;
		m_current_framebuffer_context = context;
		if (valid_layout) m_graphics_state |= rsx::rtt_config_valid;
	}
	void rebuild_framebuffer(bool clipped_scissor)
	{
		if (m_draw_fbo) m_draw_fbo->release();
		framebuffers.push_back(std::make_unique<Framebuffer>(m_rtts.image));
		m_draw_fbo = framebuffers.back().get();
		m_fbo_images = {m_rtts.image};
		viewport_scale = resolution_scaling_config.scale_percent;
		set_scissor(clipped_scissor);
		++rebuilds;
	}
	void retire_old_images()
	{
		// Model completion-driven destruction after any number of overlay flips.
		for (auto& fbo : framebuffers)
		{
			if (fbo->image != m_rtts.image)
			{
				require(fbo->refs == 0, "replaced image still has a bound framebuffer");
				fbo->alive = false;
			}
		}
	}
};

#include "VKScalingUnderTest.inc"

int main()
{
	unsigned scenarios = 0;
	// A native loading-overlay flip can run before RSX startup copies the
	// configured scale. Even 100% changes the initial threshold from 0 to 16.
	for (auto severity : {rsx::problem_severity::low, rsx::problem_severity::moderate, rsx::problem_severity::fatal})
	for (u16 scale : {50, 75, 100, 150, 200})
	{
		vk::severity = severity;
		g_cfg.video = {scale, 16};
		VKGSRender renderer;
		renderer.resolution_scaling_config = {100, 0};
		renderer.m_rtts.before_reclaim = [&]
		{
			require(!renderer.m_draw_fbo && renderer.m_fbo_images.empty(), "boot overlay retained a guest framebuffer");
		};
		renderer.sync_config();
		require(renderer.resolution_scaling_config == rsx::surface_scaling_config_t{scale, 16}, "boot scale not applied");
		require(renderer.m_rtts.replacements == 1 && !vk::renderpass_open, "boot scale mishandled the fresh command buffer");
		renderer.sync_config();
		require(renderer.m_rtts.replacements == 1, "unchanged boot scale repeated replacement");
		++scenarios;
	}
	for (auto severity : {rsx::problem_severity::low, rsx::problem_severity::moderate, rsx::problem_severity::fatal})
	for (auto context : {VKGSRender::Context::context_draw, VKGSRender::Context::context_clear})
	for (bool delayed_draw : {false, true})
	{
		vk::severity = severity;
		g_cfg.video = {75, 16};
		VKGSRender renderer;
		renderer.m_rtts.before_reclaim = [&]
		{
			require(!renderer.m_draw_fbo && renderer.m_fbo_images.empty(), "resource reclamation retained old framebuffer binding");
			for (unsigned i = 0; i < 16; i += 2)
				require(renderer.m_textures_dirty[i], "reclamation preceded sampler invalidation");
		};
		renderer.rebuild_framebuffer(true);
		for (unsigned i = 0; i < 16; ++i)
			renderer.fs_sampler_state[i] = std::make_unique<Sampler>(i % 2 ? rsx::texture_upload_context::shader_read : rsx::texture_upload_context::framebuffer_storage);
		for (auto& sampler : renderer.vs_sampler_state)
			sampler = std::make_unique<Sampler>(rsx::texture_upload_context::framebuffer_storage);

		// No change must preserve a live binding and avoid expensive work.
		auto initial_fbo = renderer.m_draw_fbo;
		renderer.sync_config();
		require(renderer.m_draw_fbo == initial_fbo && renderer.m_rtts.replacements == 0,
			"unchanged config disturbed the binding");

		for (u16 scale : {100, 125, 150, 175, 200, 50, 75})
		for (u16 threshold : {16, 512})
		{
			g_cfg.video = {scale, threshold};
			renderer.sync_config();
			require(!renderer.m_draw_fbo && renderer.m_fbo_images.empty(), "scale change retained old framebuffer binding");
			require(renderer.m_graphics_state.test(rsx::vertex_state_dirty), "scaled point size was not invalidated");
			require(renderer.resolution_scaling_config == rsx::surface_scaling_config_t{scale, threshold}, "config not applied");
			for (unsigned i = 0; i < 16; ++i)
				require(renderer.m_textures_dirty[i] == (i % 2 == 0), "fragment descriptor invalidation mismatch");
			for (bool dirty : renderer.m_vertex_textures_dirty) require(dirty, "vertex descriptor not invalidated");
			require(renderer.m_samplers_dirty, "global sampler refresh missing");
			renderer.retire_old_images();
			if (delayed_draw)
			{
				for (int i = 0; i < 4; ++i) renderer.sync_config();
				renderer.valid_layout = false;
				renderer.prepare_rtts(context);
				require(!renderer.m_draw_fbo, "invalid layout rebound old framebuffer");
				renderer.valid_layout = true;
			}
			const auto builds = renderer.rebuilds;
			renderer.prepare_rtts(context);
			require(renderer.rebuilds == builds + 1, "unchanged guest registers bypassed framebuffer rebuild");
			require(renderer.m_draw_fbo->alive && renderer.m_draw_fbo->image == renderer.m_rtts.image, "draw uses stale target");
			require(renderer.viewport_scale == scale && renderer.scissor_scale == scale, "scaled viewport or scissor stale");
			renderer.prepare_rtts(context);
			require(renderer.rebuilds == builds + 1, "steady-state draw unnecessarily rebuilt framebuffer");
			// A shader consumes only slot 0; dormant slots must remain dirty.
			renderer.m_textures_dirty[0] = false;
			renderer.m_samplers_dirty = false;
			require(renderer.m_textures_dirty[14] && renderer.m_vertex_textures_dirty[3], "dormant descriptor invalidation lost");
			++scenarios;
		}
		require(renderer.flushes == (severity == rsx::problem_severity::low ? 0u : 28u), "memory pressure handling changed");

		// Several slider changes can occur before the paused guest draws again.
		for (u16 scale : {100, 125, 150, 175, 200, 75})
		{
			g_cfg.video.resolution_scale_percent = scale;
			renderer.sync_config();
			renderer.retire_old_images();
			++scenarios;
		}
		renderer.prepare_rtts(context);
		require(renderer.m_draw_fbo->alive && renderer.m_draw_fbo->image == renderer.m_rtts.image,
			"consecutive paused scale changes retained stale framebuffer");
	}
	std::cout << "Passed " << scenarios << " Vulkan scale-transition scenarios (fake GPU resources).\n";
}
