#include "util/types.hpp"
#include "util/endian.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <random>
#include <span>
#include <stdexcept>
#include <vector>
#define ensure(value, ...)                      \
	do                                          \
	{                                           \
		if (!(value))                           \
			throw std::runtime_error("ensure"); \
	} while (false)
#include "Emu/RSX/Common/simple_array.hpp"
namespace fmt
{
	template <class... T>
	[[noreturn]] void throw_exception(const char* s, T...)
	{
		throw std::runtime_error(s);
	}
} // namespace fmt
namespace rsx
{
	namespace limits
	{
		constexpr unsigned vertex_count = 16;
	}
	namespace constants
	{
		constexpr u32 local_mem_base = 0xc0000000u;
	}
	constexpr unsigned CELL_GCM_LOCATION_LOCAL = 0;
	enum class vertex_base_type : u8
	{
		s1 = 1,
		f = 2,
		sf = 3,
		ub = 4,
		s32k = 5,
		cmp = 6,
		ub256 = 7
	};
	enum class draw_command
	{
		array,
		indexed,
		inlined_array
	};
	enum class index_array_type
	{
		u16,
		u32
	};
	enum attribute_buffer_placement : u8
	{
		none,
		persistent,
		transient
	};
	struct array_info
	{
		u32 sz = 0, st = 0, off = 0, freq = 1;
		vertex_base_type ty = vertex_base_type::f;
		u32 size() const
		{
			return sz;
		}
		u32 stride() const
		{
			return st;
		}
		u32 offset() const
		{
			return off;
		}
		u16 frequency() const
		{
			return static_cast<u16>(freq);
		}
		vertex_base_type type() const
		{
			return ty;
		}
	};
	struct register_info
	{
		u32 size = 0;
		vertex_base_type type = vertex_base_type::f;
	};
	struct clause
	{
		draw_command command = draw_command::array;
		bool is_immediate_draw = false;
	};
	struct rsx_state
	{
		std::array<array_info, 16> vertex_arrays_info{};
		std::array<register_info, 16> register_vertex_info{};
		clause current_draw_clause;
		u32 mask = 0, mod = 0, base = 0;
		bool restart = false;
		u32 restart_value = ~0u;
		index_array_type it = index_array_type::u16;
		u32 vertex_attrib_input_mask() const
		{
			return mask;
		}
		u32 frequency_divider_operation_mask() const
		{
			return mod;
		}
		u32 vertex_data_base_offset() const
		{
			return base;
		}
		index_array_type index_type() const
		{
			return it;
		}
		bool restart_index_enabled() const
		{
			return restart;
		}
		u32 restart_index() const
		{
			return restart_value;
		}
		u32 index_array_address() const
		{
			return 0;
		}
		u32 index_array_location() const
		{
			return 0;
		}
	};
	rsx_state method_registers;
	struct push_info
	{
		u32 vertex_count = 0;
		std::vector<u32> data;
		vertex_base_type type = vertex_base_type::f;
		u32 size = 0;
		std::vector<u32> pads;
		void pad_to(u32 n, bool)
		{
			pads.push_back(n);
			data.resize(n * 4);
		}
	};
	u32 get_address(u32 x, u32 location)
	{
		return x + (location ? 0x10000 : 0);
	}
	u32 get_vertex_offset_from_base(u32 b, u32 o)
	{
		return b + o;
	}
	u32 get_location(u32 x)
	{
		return x >= constants::local_mem_base ? CELL_GCM_LOCATION_LOCAL : 1;
	}
	std::vector<u32> push_indices;
	struct fake_processor
	{
		std::span<const u32> element_push_buffer()
		{
			return push_indices;
		}
	};
	struct fake_renderer
	{
		u32 local_mem_size = 0x1000000;
		fake_processor processor;
		fake_processor* draw_processor()
		{
			return &processor;
		}
	} renderer;
	fake_renderer* get_current_renderer()
	{
		return &renderer;
	}
#include "old-type.inc"
} // namespace rsx
using namespace rsx;
namespace utils
{
	template <class T>
	T aligned_div(T x, std::type_identity_t<T> d)
	{
		return x / d + T{!!(x % d)};
	}
	template <class T>
	T add_saturate(T a, T b)
	{
		return a > std::numeric_limits<T>::max() - b ? std::numeric_limits<T>::max() : a + b;
	}
} // namespace utils
namespace vm
{
	alignas(128) std::array<std::byte, 131072> memory{};
	template <class T>
	T* get_super_ptr(u32 x)
	{
		return reinterpret_cast<T*>(memory.data() + (x % memory.size()));
	}
} // namespace vm
template <class T>
T read_from_ptr_unsafe(const std::byte* p, std::size_t i)
{
	T v;
	std::memcpy(&v, p + i, sizeof(v));
	return v;
}
#define REGS(ctx) (ctx)
struct vertex_program_metadata_t
{
	u16 referenced_inputs_mask = 0;
};
struct interleaved_attribute_t
{
	u8 index;
	bool modulo;
	u16 frequency;
};
#define RANGE_FIELDS                                                                         \
	u32 attribute_stride = 0, base_offset = 0, memory_location = 0, real_offset_address = 0; \
	bool single_vertex = false, interleaved = false;                                         \
	std::pair<u32, u32> vertex_range{};                                                      \
	rsx::simple_array<interleaved_attribute_t> locations;                                    \
	std::pair<u32, u32> calculate_required_range(u32, u32);
#define PROCESSOR_FIELDS                                                                     \
	rsx_state* m_ctx;                                                                        \
	std::array<push_info, 16> m_vertex_push_buffers;                                         \
	void analyse_inputs_interleaved(vertex_input_layout&, const vertex_program_metadata_t&); \
	void fill_vertex_layout_state(const vertex_input_layout&, const vertex_program_metadata_t&, u32, u32, s32*, u32, u32) const;
struct slot
{
	float scale[3], bias[3], clamp_min[2], clamp_max[2];
	u32 remap, control;
};
static_assert(sizeof(slot) == 48);
#define TEXTURE_FIELDS     \
	using TIU_slot = slot; \
	static void masked_transfer(void*, const void*, u16);
namespace old
{
#include "old-index-size.inc"
	struct interleaved_range_info
	{
		RANGE_FIELDS
	};
#include "old-layout.inc"
	struct draw_command_processor
	{
		PROCESSOR_FIELDS
	};
#include "old-analyse.inc"
#include "old-range.inc"
#include "old-fill.inc"
	struct fragment_program_texture_config
	{
		TEXTURE_FIELDS
	};
#include "old-transfer.inc"
} // namespace old
namespace candidate
{
#include "new-index-size.inc"
#include "new-type.inc"
	struct interleaved_range_info
	{
		RANGE_FIELDS
#include "new-fast-range.inc"
	};
#include "new-layout.inc"
	struct draw_command_processor
	{
		PROCESSOR_FIELDS
	};
#include "new-analyse.inc"
#include "new-range.inc"
#include "new-fill.inc"
	struct fragment_program_texture_config
	{
		TEXTURE_FIELDS
	};
#include "new-transfer.inc"
} // namespace candidate
static volatile u64 sink;
template <class F>
double measure(F f, unsigned n)
{
	u64 r = 0;
	auto t = std::chrono::steady_clock::now();
	for (unsigned i = 0; i < n; ++i)
	{
		asm volatile("" ::: "memory");
		r += f(i);
	}
	sink = r;
	return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t).count() / n;
}
template <class A, class B>
void bench(const char* name, A a, B b, unsigned count = 300000)
{
	std::vector<double> t[2];
	for (unsigned round = 0; round < 12; ++round)
		for (unsigned order = 0; order < 2; ++order)
		{
			auto v = (round + order) % 2;
			double n = v ? measure(b, count) : measure(a, count);
			if (round >= 2)
				t[v].push_back(n);
		}
	for (auto& v : t)
		std::sort(v.begin(), v.end());
	double x = (t[0][4] + t[0][5]) / 2, y = (t[1][4] + t[1][5]) / 2;
	std::printf("%s baseline=%.3f candidate=%.3f ns/call saving=%.2f%%\n", name, x, y, 100 * (1 - y / x));
}
template <class A, class B>
void same(const A& a, const B& b)
{
	assert(a.attribute_mask == b.attribute_mask && a.attribute_placement == b.attribute_placement);
	assert(a.volatile_blocks == b.volatile_blocks);
	assert(std::equal(a.referenced_registers.begin(), a.referenced_registers.end(), b.referenced_registers.begin(), b.referenced_registers.end()));
	assert(a.interleaved_blocks.size() == b.interleaved_blocks.size());
	for (unsigned i = 0; i < a.interleaved_blocks.size(); ++i)
	{
		auto& x = *a.interleaved_blocks[i];
		auto& y = *b.interleaved_blocks[i];
		assert(x.attribute_stride == y.attribute_stride && x.base_offset == y.base_offset && x.memory_location == y.memory_location && x.real_offset_address == y.real_offset_address && x.single_vertex == y.single_vertex && x.interleaved == y.interleaved);
		assert(x.locations.size() == y.locations.size());
		for (unsigned j = 0; j < x.locations.size(); ++j)
		{
			auto u = x.locations[j], v = y.locations[j];
			assert(u.index == v.index && u.modulo == v.modulo && u.frequency == v.frequency);
		}
	}
}
int main(int argc, char**)
{
	std::mt19937 rng(310);
	bool benchmark = argc > 1;
	old::draw_command_processor op;
	candidate::draw_command_processor np;
	rsx_state state;
	op.m_ctx = np.m_ctx = &state;
	old::vertex_input_layout ol;
	candidate::vertex_input_layout nl;
	std::array<s32, 32> ob, nb;
	for (unsigned n = 0; n < 100000; ++n)
	{
		state.mask = rng() & 65535;
		state.mod = rng() & 65535;
		state.base = rng() % 0x10000;
		state.current_draw_clause.command = draw_command(n % 3);
		state.current_draw_clause.is_immediate_draw = n % 4 == 0;
		vertex_program_metadata_t meta{u16(rng())};
		for (unsigned i = 0; i < 16; ++i)
		{
			auto& v = state.vertex_arrays_info[i];
			v.sz = rng() % 5;
			v.ty = vertex_base_type(1 + rng() % 7);
			if (v.ty == vertex_base_type::ub256)
				v.sz = 4;
			v.st = (rng() % 5) * 16;
			v.off = ((rng() % 16) * 16) | ((rng() % 2) << 31);
			v.freq = rng() % 9;
			state.register_vertex_info[i] = {rng() % 5, vertex_base_type::f};
			op.m_vertex_push_buffers[i].vertex_count = np.m_vertex_push_buffers[i].vertex_count = rng() % 4;
			op.m_vertex_push_buffers[i].size = np.m_vertex_push_buffers[i].size = 4;
			op.m_vertex_push_buffers[i].data.resize(rng() % 16);
			np.m_vertex_push_buffers[i].data = op.m_vertex_push_buffers[i].data;
			op.m_vertex_push_buffers[i].pads.clear();
			np.m_vertex_push_buffers[i].pads.clear();
		}
		op.analyse_inputs_interleaved(ol, meta);
		np.analyse_inputs_interleaved(nl, meta);
		same(ol, nl);
		assert(ol.validate() == nl.validate());
		for (unsigned i = 0; i < 16; ++i)
			assert(op.m_vertex_push_buffers[i].data == np.m_vertex_push_buffers[i].data && op.m_vertex_push_buffers[i].pads == np.m_vertex_push_buffers[i].pads);
		rsx::method_registers = state;
		ob.fill(0x55555555);
		nb = ob;
		op.fill_vertex_layout_state(ol, meta, 0, 64, ob.data(), 256, 8192);
		np.fill_vertex_layout_state(nl, meta, 0, 64, nb.data(), 256, 8192);
		assert(ob == nb);
		same(ol, nl);
	}
	for (unsigned type = 0; type < 256; ++type)
		for (u32 size : {0u, 1u, 2u, 3u, 4u, 5u, 255u, 0x40000000u, ~0u})
		{
			std::string errors[2];
			u32 values[2]{};
			for (unsigned v = 0; v < 2; ++v)
				try
				{
					values[v] = v ? candidate::get_vertex_type_size_on_host(vertex_base_type(type), size) : rsx::get_vertex_type_size_on_host(vertex_base_type(type), size);
				}
				catch (const std::exception& e)
				{
					errors[v] = e.what();
				}
			assert(errors[0] == errors[1] && values[0] == values[1]);
		}
	for (unsigned type = 0; type < 256; ++type)
	{
		std::string errors[2]; u32 values[2]{};
		for (unsigned v = 0; v < 2; ++v)
			try { values[v] = v ? candidate::get_index_type_size(rsx::index_array_type(type)) : old::get_index_type_size(rsx::index_array_type(type)); }
			catch (const std::exception& e) { errors[v] = e.what(); }
		assert(errors[0] == errors[1] && values[0] == values[1]);
	}
	alignas(16) std::array<u8, 16 * 48 + 32> src, dst1, dst2;
	for (auto& v : src)
		v = rng();
	for (unsigned mask = 0; mask < 65536; ++mask)
		for (unsigned off : {0u, 1u, 15u})
		{
			dst1.fill(0xa5);
			dst2 = dst1;
			old::fragment_program_texture_config::masked_transfer(dst1.data() + off, src.data() + off, u16(mask));
			candidate::fragment_program_texture_config::masked_transfer(dst2.data() + off, src.data() + off, u16(mask));
			assert(dst1 == dst2);
		}
	// Full range routine, including modulo re-evaluation, cached/single, boundaries and errors.
	for (unsigned n = 0; n < 100000; ++n)
	{
		old::interleaved_range_info o;
		candidate::interleaved_range_info c;
		o.single_vertex = c.single_vertex = n % 29 == 0;
		o.attribute_stride = c.attribute_stride = 16;
		o.real_offset_address = c.real_offset_address = n % 2 ? 0xffff0 : 0xc0000000;
		method_registers.current_draw_clause.command = draw_command(n % 2);
		method_registers.it = n % 3 ? index_array_type::u16 : index_array_type::u32;
		method_registers.restart = n % 7 == 0;
		method_registers.restart_value = 0;
		u32 first = rng() % 256, count = rng() % 256;
		for (unsigned j = 0, end = rng() % 16; j < end; ++j)
		{
			interleaved_attribute_t a{u8(j), bool(rng() % 2), u16(rng() % 16)};
			o.locations.push_back(a);
			c.locations.push_back(a);
		}
		std::string errors[2];
		std::pair<u32, u32> values[2];
		for (unsigned v = 0; v < 2; ++v)
			try
			{
				values[v] = v ? c.get_required_range(first, count) : o.calculate_required_range(first, count);
			}
			catch (const std::exception& e)
			{
				errors[v] = e.what();
			}
		assert(errors[0] == errors[1]);
		if (errors[0].empty())
		{
			assert(values[0] == values[1] && o.vertex_range == c.vertex_range);
			assert(o.calculate_required_range(first + 1, count + 1) == c.get_required_range(first + 1, count + 1));
		}
	}
	std::puts("PASS: full production analyse/fill/validate 100k layouts; type values/errors; all 65536 texture masks; full range/cache/modulo/error cases");
	if (!benchmark)
		return 0;
	state = {};
	state.mask = 0x8001;
	vertex_program_metadata_t meta{0x8001};
	state.current_draw_clause.command = draw_command::array;
	for (unsigned i = 0; i < 16; ++i)
	{
		state.vertex_arrays_info[i] = {4, 16, i * 32, 1, vertex_base_type::f};
	}
	method_registers = state;
	bench("analyse_sparse", [&](unsigned i)
		{
			state.base = i & 255;
			op.analyse_inputs_interleaved(ol, meta);
			return ol.interleaved_blocks.size();
		},
		[&](unsigned i)
		{
			state.base = i & 255;
			np.analyse_inputs_interleaved(nl, meta);
			return nl.interleaved_blocks.size();
		});
	op.analyse_inputs_interleaved(ol, meta);
	np.analyse_inputs_interleaved(nl, meta);
	bench("fill_sparse", [&](unsigned i)
		{
			op.fill_vertex_layout_state(ol, meta, 0, 64, ob.data(), i & 255, 8192);
			return u32(ob[30]);
		},
		[&](unsigned i)
		{
			np.fill_vertex_layout_state(nl, meta, 0, 64, nb.data(), i & 255, 8192);
			return u32(nb[30]);
		});
	state.mask = meta.referenced_inputs_mask = 0xffff;
	bench("analyse_dense", [&](unsigned i)
		{
			state.base = i & 255;
			op.analyse_inputs_interleaved(ol, meta);
			return ol.interleaved_blocks.size();
		},
		[&](unsigned i)
		{
			state.base = i & 255;
			np.analyse_inputs_interleaved(nl, meta);
			return nl.interleaved_blocks.size();
		});
	op.analyse_inputs_interleaved(ol, meta);
	np.analyse_inputs_interleaved(nl, meta);
	bench("fill_dense", [&](unsigned i)
		{
			op.fill_vertex_layout_state(ol, meta, 0, 64, ob.data(), i & 255, 8192);
			return u32(ob[30]);
		},
		[&](unsigned i)
		{
			np.fill_vertex_layout_state(nl, meta, 0, 64, nb.data(), i & 255, 8192);
			return u32(nb[30]);
		});
	old::interleaved_range_info orng;
	candidate::interleaved_range_info nrng;
	for (unsigned i = 0; i < 4; ++i)
	{
		orng.locations.push_back({u8(i), false, 1});
		nrng.locations.push_back({u8(i), false, 1});
	}
	bench("range_unit_frequency", [&](unsigned i)
		{
			orng.vertex_range.second = 0;
			return orng.calculate_required_range(i & 255, 64).second;
		},
		[&](unsigned i)
		{
			nrng.vertex_range.second = 0;
			return nrng.calculate_required_range(i & 255, 64).second;
		});
	orng.vertex_range = nrng.vertex_range = {17, 64};
	bench("range_cached", [&](unsigned i)
		{
			return orng.calculate_required_range(i & 255, 64).second;
		},
		[&](unsigned i)
		{
			return nrng.get_required_range(i & 255, 64).second;
		});
	ol.interleaved_blocks.clear();
	nl.interleaved_blocks.clear();
	ol.volatile_blocks.clear();
	nl.volatile_blocks.clear();
	ol.referenced_registers.clear();
	nl.referenced_registers.clear();
	ol.attribute_placement.fill(attribute_buffer_placement::none);
	nl.attribute_placement.fill(attribute_buffer_placement::none);
	ol.attribute_placement[15] = nl.attribute_placement[15] = attribute_buffer_placement::persistent;
	bench("native_validate_sparse", [&](unsigned i)
		{
			ol.attribute_mask = 0x8000 | (1u << (i % 8));
			return ol.validate();
		},
		[&](unsigned i)
		{
			nl.attribute_mask = 0x8000 | (1u << (i % 8));
			return nl.validate();
		});
	bench("index_type", [&](unsigned i) { return old::get_index_type_size(rsx::index_array_type(i & 1)); }, [&](unsigned i) { return candidate::get_index_type_size(rsx::index_array_type(i & 1)); });
	bench("vertex_type", [&](unsigned i)
		{
			return rsx::get_vertex_type_size_on_host(vertex_base_type(1 + i % 7), i % 7 == 6 ? 4 : 1 + (i / 7) % 4);
		},
		[&](unsigned i)
		{
			return candidate::get_vertex_type_size_on_host(vertex_base_type(1 + i % 7), i % 7 == 6 ? 4 : 1 + (i / 7) % 4);
		});
	bench("transfer_single", [&](unsigned i)
		{
			old::fragment_program_texture_config::masked_transfer(dst1.data(), src.data(), u16(1u << (4 + i % 12)));
			return dst1[i % 768];
		},
		[&](unsigned i)
		{
			candidate::fragment_program_texture_config::masked_transfer(dst2.data(), src.data(), u16(1u << (4 + i % 12)));
			return dst2[i % 768];
		});
	bench("transfer_common", [&](unsigned i)
		{
			old::fragment_program_texture_config::masked_transfer(dst1.data(), src.data(), u16((1u << (1 + i % 4)) - 1));
			return dst1[i % 768];
		},
		[&](unsigned i)
		{
			candidate::fragment_program_texture_config::masked_transfer(dst2.data(), src.data(), u16((1u << (1 + i % 4)) - 1));
			return dst2[i % 768];
		});
}
