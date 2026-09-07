#include "Emu/Memory/VMReservationRange.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <random>

namespace
{
	using u32 = std::uint32_t;
	using u64 = std::uint64_t;
	std::array<u64, 65536> mirrors{};
	std::size_t cases = 0;
	std::size_t false_conflicts_removed = 0;

	u64 reservation_at(u64 address)
	{
		assert(address < 0x1'0000'0000);
		const auto mirror = mirrors[address >> 16];
		return (mirror ? mirror | (address & 0xffff) : address) / 128;
	}

	// Independent oracle: enumerate the guest reservation lines touched by the
	// range, translate each line, and compare its index directly.
	bool oracle(u64 point, u32 address, u32 size)
	{
		if (!size) return false;
		const u64 end = u64{address} + size;
		for (u64 line = u64{address} & ~u64{127}; line < end; line += 128)
		{
			if (reservation_at(line) == point) return true;
		}
		return false;
	}

	bool legacy(u64 point, u64 address, u32 size)
	{
		if (!size) return false;
		for (u64 page = address >> 16, last = (address + size - 1) >> 16; page <= last; page++)
		{
			u64 mapped = address;
			const u64 chunk = std::min<u64>(address + size, (address + 0xffff) & ~u64{0xffff}) - address;
			if (const auto mirror = mirrors[page]) mapped = (address & 0xffff) | mirror;
			if (point - mapped / 128 <= (mapped + chunk - 1) / 128 - mapped / 128) return true;
			address += chunk;
			size -= static_cast<u32>(chunk);
		}
		return false;
	}

	void check(u64 point, u32 address, u32 size)
	{
		assert(u64{address} + size <= 0x1'0000'0000);
		u64 lookups = 0;
		u64 next_page = address >> 16;
		const bool actual = vm::reservation_range_overlaps(point, address, size, [&](u64 page)
		{
			assert(page == next_page++);
			assert(page < mirrors.size());
			lookups++;
			return mirrors[page];
		});
		const bool expected = oracle(point, address, size);
		assert(actual == expected);
		const u64 page_count = size ? ((u64{address} + size - 1) >> 16) - (address >> 16) + 1 : 0;
		assert(lookups <= page_count);
		if (!actual) assert(lookups == page_count);
		const bool before = legacy(point, address, size);
		assert(!expected || before); // Existing genuine conflicts are preserved.
		false_conflicts_removed += before && !actual;
		cases++;
	}
}

int main()
{
	constexpr auto unshared = [](u64) { return u64{0}; };
	static_assert(!vm::reservation_range_overlaps(0x90000 / 128, 0x10000, 128, unshared));
	static_assert(vm::reservation_range_overlaps(0x10000 / 128, 0x10000, 128, unshared));
	static_assert(!vm::reservation_range_overlaps(0x90000 / 128, 0x1fff0, 32, unshared));
	static_assert(vm::reservation_range_overlaps(0x20000 / 128, 0x1fff0, 32, unshared));
	assert(legacy(0x90000 / 128, 0x10000, 128));
	assert(legacy(0x90000 / 128, 0x1fff0, 32));

	// Include page zero, adjacent pages, shared aliases, and the final guest page.
	for (int shared = 0; shared < 2; shared++)
	{
		for (u64 page = 0; page < mirrors.size(); page++)
			mirrors[page] = shared && page % 3 ? 0x1'0000'0000 + (page % 257) * 0x10000 : 0;

		for (u64 base : {u64{0}, u64{0x10000}, u64{0x20000}, u64{0xffff0000}})
		for (u64 offset : {u64{0}, u64{1}, u64{127}, u64{128}, u64{129}, u64{0xff00}, u64{0xff7f}, u64{0xff80}, u64{0xff81}, u64{0xfffe}, u64{0xffff}})
		for (u32 size : {0u, 1u, 2u, 127u, 128u, 129u, 255u, 256u, 65535u, 65536u, 65537u, 131073u})
		{
			const auto start = static_cast<u32>(base + offset);
			const u64 end = u64{start} + size;
			if (end > 0x1'0000'0000) continue;
			check(0x90000 / 128, start, size);
			check(reservation_at(start), start, size);
			if (size) check(reservation_at(end - 1), start, size);
			if (start) check(reservation_at(start - 1), start, size);
			if (end < 0x1'0000'0000) check(reservation_at(end), start, size);
		}
	}

	std::mt19937_64 rng(0x17164128);
	for (u32 i = 0; i < 200000; i++)
	{
		u32 start = static_cast<u32>(rng());
		if (i % 4 == 0) start &= 0xffff0000;
		const auto size = static_cast<u32>(std::min<u64>(rng() % 0x30001, 0x1'0000'0000 - start));
		const u64 point = i % 2 && size ? reservation_at(u64{start} + rng() % size) : reservation_at(static_cast<u32>(rng()));
		check(point, start, size);
	}

	// Long ranges and the inclusive endpoint at 4 GiB must neither wrap nor
	// perform an out-of-bounds shared-memory lookup.
	mirrors.fill(0);
	check(0, 0, std::numeric_limits<u32>::max());
	check(0xffffffff / 128, 1, std::numeric_limits<u32>::max());
	check(0x1'0000'0000 / 128, 1, std::numeric_limits<u32>::max());
	check(0x90000 / 128, 0xffff0000, 65536);

	std::printf("VM reservation range: %zu oracle cases passed; %zu legacy false conflicts removed\n", cases, false_conflicts_removed);
}
