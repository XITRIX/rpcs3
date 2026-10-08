#include "util/types.hpp"
#include <stdexcept>
#define ensure(value, ...)                      \
	do                                          \
	{                                           \
		if (!(value))                           \
			throw std::runtime_error("ensure"); \
	} while (false)
#include "Emu/RSX/Common/simple_array.hpp"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <random>
#include <variant>
#include <vector>
using VkSampler = void*;
using VkImageView = void*;
using VkBufferView = void*;
using VkBuffer = void*;
using VkImageLayout = u32;
constexpr auto VK_NULL_HANDLE = nullptr;
struct VkDescriptorImageInfo
{
	VkSampler sampler = nullptr;
	VkImageView imageView = nullptr;
	VkImageLayout imageLayout = 0;
};
struct VkDescriptorBufferInfo
{
	VkBuffer buffer = nullptr;
	u64 offset = 0, range = 0;
};
namespace vk
{
	struct image_view;
	struct sampler;
	struct buffer;
	struct buffer_view;
#include "types.inc"
} // namespace vk
using namespace vk;
using descriptor_image_array_t = rsx::simple_array<VkDescriptorImageInfoEx>;
using descriptor_slot_t = std::variant<VkDescriptorImageInfoEx, VkDescriptorBufferInfoEx, VkDescriptorBufferViewEx, descriptor_image_array_t>;
struct table
{
	std::vector<descriptor_slot_t> m_descriptor_slots;
	std::vector<u8> m_descriptors_dirty;
	bool m_any_descriptors_dirty = false;
	template <class T>
	void notify_descriptor_slot_updated(u32 slot, const T& data)
	{
		m_descriptor_slots[slot] = data;
		m_descriptors_dirty[slot] = true;
		m_any_descriptors_dirty = true;
	}
};
#define METHODS                                                   \
	void bind_uniform(const VkDescriptorImageInfoEx&, u32, u32);  \
	void bind_uniform(const VkDescriptorBufferInfoEx&, u32, u32); \
	void bind_uniform(const VkDescriptorBufferViewEx&, u32, u32);
namespace old
{
#include "old-eq-ImageInfo.inc"
#include "old-eq-BufferInfo.inc"
#include "old-eq-BufferView.inc"
	struct program
	{
		std::array<table, 2> m_sets;
		METHODS
	};
#include "old-bind-ImageInfo.inc"
#include "old-bind-BufferInfo.inc"
#include "old-bind-BufferView.inc"
} // namespace old
namespace candidate
{
#include "new-eq-ImageInfo.inc"
#include "new-eq-BufferInfo.inc"
#include "new-eq-BufferView.inc"
	struct program
	{
		std::array<table, 2> m_sets;
#include "new-bind-ImageInfo.inc"
#include "new-bind-BufferInfo.inc"
#include "new-bind-BufferView.inc"
	};
} // namespace candidate
static volatile u64 sink;
template <class A, class B>
void bench(const char* name, A a, B b)
{
	std::vector<double> times[2];
	for (unsigned r = 0; r < 12; ++r)
		for (unsigned j = 0; j < 2; ++j)
		{
			unsigned v = (r + j) % 2;
			auto t = std::chrono::steady_clock::now();
			u64 s = 0;
			for (unsigned i = 0; i < 1000000; ++i)
			{
				asm volatile("" ::: "memory");
				s += v ? b(i) : a(i);
			}
			sink = s;
			double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t).count() / 1000000;
			if (r >= 2)
				times[v].push_back(ns);
		}
	for (auto& v : times)
		std::sort(v.begin(), v.end());
	double x = times[0][5], y = times[1][5];
	std::printf("%s baseline=%.3f candidate=%.3f ns saving=%.2f%%\n", name, x, y, 100 * (1 - y / x));
}
bool equal(const descriptor_slot_t& a, const descriptor_slot_t& b)
{
	if (a.index() != b.index())
		return false;
	if (a.index() == 0)
	{
		auto& x = std::get<0>(a);
		auto& y = std::get<0>(b);
		return x.sampler == y.sampler && x.imageView == y.imageView && x.imageLayout == y.imageLayout && x.resourceId == y.resourceId;
	}
	if (a.index() == 1)
	{
		auto& x = std::get<1>(a);
		auto& y = std::get<1>(b);
		return x.buffer == y.buffer && x.offset == y.offset && x.range == y.range && x.resourceId == y.resourceId;
	}
	if (a.index() == 2)
	{
		auto& x = std::get<2>(a);
		auto& y = std::get<2>(b);
		return x.view == y.view && x.resourceId == y.resourceId;
	}
	return std::get<3>(a).size() == std::get<3>(b).size();
}
int main(int argc, char**)
{
	old::program o;
	candidate::program n;
	for (auto* sets : {&o.m_sets, &n.m_sets})
		for (auto& s : *sets)
		{
			s.m_descriptor_slots.resize(128);
			s.m_descriptors_dirty.resize(128);
		}
	std::mt19937_64 rng(310);
	VkDescriptorImageInfoEx image;
	VkDescriptorBufferInfoEx buffer;
	VkDescriptorBufferViewEx view;
	for (unsigned i = 0; i < 200000; ++i)
	{
		unsigned set = rng() % 2, slot = rng() % 128;
		image.sampler = reinterpret_cast<void*>(rng() % 32);
		image.imageView = reinterpret_cast<void*>(rng() % 32);
		image.imageLayout = rng() % 8;
		image.resourceId = rng() % 16;
		buffer.buffer = reinterpret_cast<void*>(rng() % 32);
		buffer.offset = rng() % 16;
		buffer.range = rng() % 16;
		buffer.resourceId = rng() % 16;
		view.view = reinterpret_cast<void*>(rng() % 32);
		view.resourceId = rng() % 16;
		switch (rng() % 4)
		{
		case 0:
			o.bind_uniform(image, set, slot);
			n.bind_uniform(image, set, slot);
			break;
		case 1:
			o.bind_uniform(buffer, set, slot);
			n.bind_uniform(buffer, set, slot);
			break;
		case 2:
			o.bind_uniform(view, set, slot);
			n.bind_uniform(view, set, slot);
			break;
		default:
			o.m_sets[set].m_descriptor_slots[slot] = descriptor_image_array_t{};
			n.m_sets[set].m_descriptor_slots[slot] = descriptor_image_array_t{};
			break;
		}
		for (unsigned s = 0; s < 2; ++s)
		{
			assert(o.m_sets[s].m_descriptors_dirty == n.m_sets[s].m_descriptors_dirty && o.m_sets[s].m_any_descriptors_dirty == n.m_sets[s].m_any_descriptors_dirty);
			for (unsigned j = 0; j < 128; ++j)
				assert(equal(o.m_sets[s].m_descriptor_slots[j], n.m_sets[s].m_descriptor_slots[j]));
		}
		if (i % 31 == 0)
			for (auto* sets : {&o.m_sets, &n.m_sets})
				for (auto& s : *sets)
				{
					std::fill(s.m_descriptors_dirty.begin(), s.m_descriptors_dirty.end(), 0);
					s.m_any_descriptors_dirty = false;
				}
	}
	std::puts("PASS: three extracted descriptor binding paths; 200k mixed type/resource mutations; all slots and dirty states");
	if (argc == 1)
		return 0;
	bench("bind_buffer_changed", [&](unsigned i)
		{
			buffer.offset = i;
			o.bind_uniform(buffer, 1, i % 32);
			return o.m_sets[1].m_descriptors_dirty[i % 32];
		},
		[&](unsigned i)
		{
			buffer.offset = i;
			n.bind_uniform(buffer, 1, i % 32);
			return n.m_sets[1].m_descriptors_dirty[i % 32];
		});
	bench("bind_image_changed", [&](unsigned i)
		{
			image.resourceId = i;
			o.bind_uniform(image, 1, i % 32);
			return o.m_sets[1].m_descriptors_dirty[i % 32];
		},
		[&](unsigned i)
		{
			image.resourceId = i;
			n.bind_uniform(image, 1, i % 32);
			return n.m_sets[1].m_descriptors_dirty[i % 32];
		});
	o.bind_uniform(view, 1, 0);
	n.bind_uniform(view, 1, 0);
	bench("bind_view_unchanged", [&](unsigned)
		{
			o.bind_uniform(view, 1, 0);
			return o.m_sets[1].m_descriptors_dirty[0];
		},
		[&](unsigned)
		{
			n.bind_uniform(view, 1, 0);
			return n.m_sets[1].m_descriptors_dirty[0];
		});
}
