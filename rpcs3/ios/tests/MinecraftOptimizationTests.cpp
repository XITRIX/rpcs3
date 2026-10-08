#include <arm_neon.h>
#include "util/v128.hpp"
#include "Emu/RSX/Common/unordered_map.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>
#ifndef FORCE_INLINE
#define FORCE_INLINE inline __attribute__((always_inline))
#endif
using spu_rdata_t = std::byte[128];
namespace old_cmp
{
#include "old-cmp.inc"
}
namespace candidate_cmp
{
#include "new-cmp.inc"
}
namespace vm
{
	alignas(128) std::array<char, 1024 * 1024> memory{};
	template <class T>
	T* get_super_ptr(u32 address)
	{
		assert(address < memory.size());
		return reinterpret_cast<T*>(memory.data() + address);
	}
} // namespace vm
namespace utils
{
	template <class T, class U>
	T* bless(U* ptr)
	{
		return reinterpret_cast<T*>(ptr);
	}
} // namespace utils
struct uploaded_range
{
	uptr local_address;
	u32 offset_in_heap;
	u32 data_length;
	u64 fingerprint;
};
template <class T>
class default_vertex_cache
{
public:
	virtual ~default_vertex_cache() = default;
	virtual const T* find_vertex_range(u32, u32)
	{
		return nullptr;
	}
	virtual void store_range(u32, u32, u32) {}
	virtual void purge() {}
};
namespace old_cache
{
#include "old-cache.inc"
}
namespace candidate_cache
{
#include "new-cache.inc"
}
namespace fmt
{
	template <class... T>
	[[noreturn]] void throw_exception(const char* s, T...)
	{
		throw std::runtime_error(s);
	}
} // namespace fmt
enum class attribute_buffer_placement
{
	none,
	transient,
	persistent
};
struct test_block
{
	u32 attribute_stride = 0;
};
#define LAYOUT_FIELDS                            \
	std::vector<test_block*> interleaved_blocks; \
	std::vector<unsigned> volatile_blocks;       \
	std::vector<u8> referenced_registers;        \
	u16 attribute_mask = 0;                      \
	std::array<attribute_buffer_placement, 16> attribute_placement{};
struct old_layout
{
	LAYOUT_FIELDS
#include "old-validate.inc"
};
struct candidate_layout
{
	LAYOUT_FIELDS
#include "new-validate.inc"
};
struct old_dirty
{
	std::vector<bool> m_descriptors_dirty;
	std::vector<u64> m_descriptor_slots;
	bool m_any_descriptors_dirty = false;
	template <class T>
#include "old-dirty.inc"
};
struct candidate_dirty
{
#include "dirty-type.inc"
	dirty_type m_descriptors_dirty;
	std::vector<u64> m_descriptor_slots;
	bool m_any_descriptors_dirty = false;
	template <class T>
#include "new-dirty.inc"
};
static volatile u64 sink;
template <class F>
double measure(F f, unsigned count)
{
	u64 r = 0;
	auto t = std::chrono::steady_clock::now();
	for (unsigned i = 0; i < count; ++i)
	{
		asm volatile("" ::: "memory");
		r += f(i);
	}
	sink = r;
	return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t).count() / count;
}
template <class A, class B>
void bench(const char* name, A a, B b, unsigned count = 500000)
{
	std::vector<double> times[2];
	for (unsigned round = 0; round < 12; ++round)
		for (unsigned order = 0; order < 2; ++order)
		{
			unsigned v = (round + order) % 2;
			double n = v ? measure(b, count) : measure(a, count);
			if (round >= 2)
				times[v].push_back(n);
		}
	for (auto& x : times)
		std::sort(x.begin(), x.end());
	double x = (times[0][4] + times[0][5]) / 2, y = (times[1][4] + times[1][5]) / 2;
	std::printf("%s baseline=%.3f candidate=%.3f ns/call saving=%.2f%%\n", name, x, y, 100 * (1 - y / x));
}
bool same(const uploaded_range* a, const uploaded_range* b)
{
	return bool(a) == bool(b) && (!a || (a->local_address == b->local_address && a->offset_in_heap == b->offset_in_heap && a->data_length == b->data_length && a->fingerprint == b->fingerprint));
}
int main(int argc, char**)
{
	bool benchmark = argc > 1;
	std::mt19937 rng(310);
	alignas(128) std::array<std::byte, 256> a{}, b{};
	auto compare = [&](unsigned x, unsigned y)
	{
		auto& lhs = *reinterpret_cast<const spu_rdata_t*>(a.data() + x);
		auto& rhs = *reinterpret_cast<const spu_rdata_t*>(b.data() + y);
		bool expected = std::memcmp(&lhs, &rhs, 128) == 0;
		assert(old_cmp::cmp_rdata(lhs, rhs) == expected);
		assert(candidate_cmp::cmp_rdata(lhs, rhs) == expected);
	};
	for (unsigned off = 0; off <= 128; off += 16)
	{
		for (auto& v : a)
			v = std::byte(rng());
		b = a;
		compare(off, off);
		for (unsigned byte = 0; byte < 128; ++byte)
			for (unsigned bit = 0; bit < 8; ++bit)
			{
				b = a;
				b[off + byte] ^= std::byte(1u << bit);
				compare(off, off);
			}
	}
	for (unsigned i = 0; i < 20000; ++i)
	{
		for (auto& v : a)
			v = std::byte(rng());
		b = a;
		if (i % 2)
			b[rng() % 128] ^= std::byte(1);
		compare(0, 0);
	}
	auto page = sysconf(_SC_PAGESIZE);
	auto* mem = static_cast<std::byte*>(mmap(nullptr, page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0));
	assert(mem != MAP_FAILED && mprotect(mem + page, page, PROT_READ | PROT_WRITE) == 0);
	for (unsigned tail : {0u, 128u})
	{
		auto& v = *reinterpret_cast<spu_rdata_t*>(mem + page + (tail ? page - tail : 0));
		assert(old_cmp::cmp_rdata(v, v) && candidate_cmp::cmp_rdata(v, v));
	}
	munmap(mem, page * 3);
	old_cache::weak_vertex_cache oc;
	candidate_cache::weak_vertex_cache nc;
	for (unsigned i = 0; i < 200000; ++i)
	{
		u32 addr = (rng() % 512) * 128, length = rng() % 129, offset = rng();
		unsigned op = rng() % 12;
		if (op < 5)
		{
			oc.store_range(addr, length, offset);
			nc.store_range(addr, length, offset);
		}
		else if (op == 5)
		{
			vm::memory[addr] ^= 1;
		}
		else if (op == 6)
		{
			oc.purge();
			nc.purge();
		}
		else
			assert(same(oc.find_vertex_range(addr, length), nc.find_vertex_range(addr, length)));
		assert(same(oc.find_vertex_range(addr, length), nc.find_vertex_range(addr, length)));
	}
	old_layout ol;
	candidate_layout nl;
	test_block block;
	for (unsigned mask = 0; mask < 65536; ++mask)
	{
		ol.attribute_mask = nl.attribute_mask = mask;
		ol.referenced_registers.clear();
		nl.referenced_registers.clear();
		for (unsigned i = 0; i < 16; ++i)
		{
			ol.attribute_placement[i] = nl.attribute_placement[i] = attribute_buffer_placement((i + mask) % 3);
			if ((i + mask) % 4 == 0)
			{
				ol.referenced_registers.push_back(i);
				nl.referenced_registers.push_back(i);
			}
		}
		assert(ol.validate() == nl.validate());
	}
	for (unsigned i = 0; i < 100000; ++i)
	{
		ol.attribute_mask = nl.attribute_mask = rng();
		for (unsigned j = 0; j < 16; ++j)
			ol.attribute_placement[j] = nl.attribute_placement[j] = attribute_buffer_placement(rng() % 3);
		assert(ol.validate() == nl.validate());
	}
	for (unsigned stride : {0u, 16u})
	{
		block.attribute_stride = stride;
		ol.interleaved_blocks = {&block};
		nl.interleaved_blocks = {&block};
		assert(ol.validate() == nl.validate());
	}
	ol.interleaved_blocks.clear();
	nl.interleaved_blocks.clear();
	ol.volatile_blocks = {1};
	nl.volatile_blocks = {1};
	assert(ol.validate() == nl.validate());
	ol.volatile_blocks.clear();
	nl.volatile_blocks.clear();
	old_dirty od;
	candidate_dirty nd;
	od.m_descriptors_dirty.resize(256);
	nd.m_descriptors_dirty.resize(256);
	od.m_descriptor_slots.resize(256);
	nd.m_descriptor_slots.resize(256);
	for (unsigned i = 0; i < 100000; ++i)
	{
		unsigned slot = rng() % 256;
		u64 value = rng();
		od.notify_descriptor_slot_updated(slot, value);
		nd.notify_descriptor_slot_updated(slot, value);
		assert(od.m_descriptor_slots == nd.m_descriptor_slots && od.m_any_descriptors_dirty == nd.m_any_descriptors_dirty);
		for (unsigned j = 0; j < 256; ++j)
			assert(bool(od.m_descriptors_dirty[j]) == bool(nd.m_descriptors_dirty[j]));
		if (i % 33 == 0)
		{
			std::fill(od.m_descriptors_dirty.begin(), od.m_descriptors_dirty.end(), false);
			std::fill(nd.m_descriptors_dirty.begin(), nd.m_descriptors_dirty.end(), false);
		}
	}
	std::puts("PASS: production cmp 128-byte oracle/each-bit mutations/guard pages, vertex cache mutation/purge/overwrite, layout masks and descriptor state");
	if (!benchmark)
		return 0;
	for (auto& v : a)
		v = std::byte(rng());
	b = a;
	bench("cmp_equal", [&](unsigned i)
		{
			return old_cmp::cmp_rdata(*reinterpret_cast<spu_rdata_t*>(a.data() + (i % 8) * 16), *reinterpret_cast<spu_rdata_t*>(b.data() + (i % 8) * 16));
		},
		[&](unsigned i)
		{
			return candidate_cmp::cmp_rdata(*reinterpret_cast<spu_rdata_t*>(a.data() + (i % 8) * 16), *reinterpret_cast<spu_rdata_t*>(b.data() + (i % 8) * 16));
		});
	b[64] ^= std::byte(1);
	bench("cmp_different", [&](unsigned i)
		{
			return old_cmp::cmp_rdata(*reinterpret_cast<spu_rdata_t*>(a.data() + (i % 8) * 16), *reinterpret_cast<spu_rdata_t*>(b.data() + (i % 8) * 16));
		},
		[&](unsigned i)
		{
			return candidate_cmp::cmp_rdata(*reinterpret_cast<spu_rdata_t*>(a.data() + (i % 8) * 16), *reinterpret_cast<spu_rdata_t*>(b.data() + (i % 8) * 16));
		});
	oc.purge();
	nc.purge();
	bench("cache_store_hit", [&](unsigned i)
		{
			oc.store_range((i % 128) * 128, 128, i);
			return i;
		},
		[&](unsigned i)
		{
			nc.store_range((i % 128) * 128, 128, i);
			return i;
		});
	bench("cache_store_miss", [&](unsigned i)
		{
			if (i % 1024 == 0)
				oc.purge();
			oc.store_range((i % 1024) * 128, 128, i);
			return i;
		},
		[&](unsigned i)
		{
			if (i % 1024 == 0)
				nc.purge();
			nc.store_range((i % 1024) * 128, 128, i);
			return i;
		});
	bench("cache_invalidate", [&](unsigned i)
		{
			u32 addr = (i % 128) * 128;
			oc.store_range(addr, 128, i);
			vm::memory[addr] ^= 1;
			return oc.find_vertex_range(addr, 128) == nullptr;
		},
		[&](unsigned i)
		{
			u32 addr = (i % 128) * 128;
			nc.store_range(addr, 128, i);
			vm::memory[addr] ^= 1;
			return nc.find_vertex_range(addr, 128) == nullptr;
		});
	for (unsigned j = 0; j < 16; ++j)
		ol.attribute_placement[j] = nl.attribute_placement[j] = attribute_buffer_placement::none;
	ol.attribute_placement[15] = nl.attribute_placement[15] = attribute_buffer_placement::persistent;
	ol.referenced_registers.clear();
	nl.referenced_registers.clear();
	bench("layout_validate_sparse", [&](unsigned i)
		{
			ol.attribute_mask = (1u << 15) | (1u << (i % 8));
			return ol.validate();
		},
		[&](unsigned i)
		{
			nl.attribute_mask = (1u << 15) | (1u << (i % 8));
			return nl.validate();
		});
	bench("layout_validate_dense", [&](unsigned i)
		{
			ol.attribute_mask = 0xffffu ^ (i & 3);
			return ol.validate();
		},
		[&](unsigned i)
		{
			nl.attribute_mask = 0xffffu ^ (i & 3);
			return nl.validate();
		});
	bench("descriptor_dirty", [&](unsigned i)
		{
			od.notify_descriptor_slot_updated(i % 64, u64(i));
			return od.m_descriptor_slots[i % 64];
		},
		[&](unsigned i)
		{
			nd.notify_descriptor_slot_updated(i % 64, u64(i));
			return nd.m_descriptor_slots[i % 64];
		});
}
