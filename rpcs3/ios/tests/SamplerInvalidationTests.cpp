#include "Emu/RSX/Common/sampler_invalidation.h"

#include <cassert>
#include <cstdint>
#include <memory>

namespace
{
	enum class context { texture, framebuffer };
	struct sampler
	{
		context upload_context;
		unsigned image_generation = 1;
	};

	template <std::size_t Count>
	struct shader_stage
	{
		std::array<std::unique_ptr<sampler>, Count> samplers{};
		std::array<bool, Count> dirty{};
		std::array<bool, Count> enabled{};

		bool invalidate_framebuffers()
		{
			return rsx::invalidate_sampler_context(samplers, dirty, context::framebuffer);
		}

		// Model the consumer in load_texture_env: only the current shader's
		// referenced slots consume their dirty flags. Upload restores spilled
		// storage and supplies a new view, represented by its generation here.
		void load(std::uint32_t mask, bool all_dirty, unsigned resident_generation)
		{
			for (std::size_t i = 0; i < Count; ++i)
			{
				if (!(mask & (1u << i)) || (!all_dirty && !dirty[i]))
				{
					continue;
				}
				dirty[i] = false;
				if (!enabled[i])
				{
					samplers[i].reset();
				}
				else if (samplers[i])
				{
					samplers[i]->image_generation = resident_generation;
				}
			}
		}
	};
}

int main()
{
	// GT6's later draw references 0..3 and depth slot 12 (mask 0x100f).
	// A shader using only 0..3 can consume the global refresh after a spill.
	// Run with both initial global states; an already-pending refresh must
	// not suppress persistent invalidation of the unreferenced depth slot.
	for (const bool initially_dirty : {false, true})
	{
		shader_stage<16> fragment;
		shader_stage<4> vertex;
		for (std::size_t i = 0; i < 4; ++i)
		{
			fragment.samplers[i] = std::make_unique<sampler>(context::texture);
			fragment.enabled[i] = true;
		}
		fragment.samplers[12] = std::make_unique<sampler>(context::framebuffer);
		fragment.enabled[12] = true;
		// A disabled vertex texture also retains a cached view until visited.
		vertex.samplers[3] = std::make_unique<sampler>(context::framebuffer);
		fragment.dirty[2] = true; // Preserve unrelated register invalidation.

		bool all_dirty = initially_dirty;
		const bool fragment_invalidated = fragment.invalidate_framebuffers();
		const bool vertex_invalidated = vertex.invalidate_framebuffers();
		all_dirty |= fragment_invalidated || vertex_invalidated;
		assert(fragment_invalidated && vertex_invalidated);
		assert(!fragment.dirty[0] && fragment.dirty[2]);
		assert(fragment.dirty[12] && vertex.dirty[3]);
		assert(!fragment.dirty[15]); // Empty slots need no refresh.

		fragment.load(0x000f, all_dirty, 2);
		vertex.load(0, all_dirty, 2);
		all_dirty = false;
		assert(fragment.dirty[12] && vertex.dirty[3]);
		assert(fragment.samplers[12]->image_generation == 1);

		fragment.load(0x100f, all_dirty, 2);
		assert(fragment.samplers[12]->image_generation == 2);
		assert(!fragment.dirty[12]);
		vertex.load(1u << 3, all_dirty, 2);
		assert(!vertex.samplers[3] && !vertex.dirty[3]);

		// Repeated reclamation must re-invalidate every framebuffer slot,
		// including a slot refreshed since the previous spill.
		fragment.samplers[5] = std::make_unique<sampler>(context::framebuffer);
		fragment.enabled[5] = true;
		assert(fragment.invalidate_framebuffers());
		assert(fragment.dirty[5] && fragment.dirty[12]);
		fragment.load((1u << 5) | (1u << 12), false, 3);
		assert(fragment.samplers[5]->image_generation == 3);
		assert(fragment.samplers[12]->image_generation == 3);
		assert(!vertex.invalidate_framebuffers());
	}
}
