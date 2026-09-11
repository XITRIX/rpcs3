#include "../../../Utilities/JITArenaAllocator.h"
#include "../../../Utilities/JITIOSLayoutPolicy.h"

#include <cassert>

int main()
{
	using namespace rpcs3::ios::jit;

	static_assert(choose_arena_capacity(0) == 384 * mib);
	static_assert(choose_arena_capacity(4ull * 1024 * mib) == 256 * mib);
	static_assert(choose_arena_capacity(4ull * 1024 * mib, true) == 512 * mib);
	static_assert(choose_arena_capacity(6ull * 1024 * mib) == 384 * mib);
	static_assert(choose_arena_capacity(6ull * 1024 * mib, true) == 512 * mib);
	static_assert(choose_arena_capacity(8ull * 1024 * mib) == 512 * mib);
	static_assert(choose_arena_capacity(8'000'000'000ull) == 448 * mib);
	static_assert(choose_arena_capacity(8'000'000'000ull, true) == 512 * mib);
	static_assert(choose_arena_capacity(64ull * 1024 * mib) == 512 * mib);
	static_assert(choose_arena_capacity(0, 512) == 512 * mib);
	static_assert(choose_arena_capacity(0, 612) == 612 * mib);
	static_assert(choose_arena_capacity(0, 1024) == 1024 * mib);
	static_assert(choose_arena_capacity(0, 511) == 0);
	static_assert(choose_arena_capacity(0, 1025) == 0);

	for (const auto value : {"0", "1", "512", "612", "1024"})
	{
		assert(choose_arena_capacity(0, parse_expanded_arena_capacity(value)) != 0);
	}
	for (const auto value : {"", "-1", "+512", " 512", "512MiB", "511", "1025", "4294967296"})
	{
		assert(choose_arena_capacity(0, parse_expanded_arena_capacity(value)) == 0);
	}

	// Every slider value must prepare the entire code range in bounded chunks,
	// including the partial final chunk for 100 MiB recovery increments.
	for (u32 size = 512; size <= 1024; ++size)
	{
		const usz capacity = choose_arena_capacity(0, size);
		usz prepared = 0;
		for (u32 chunk = 0; chunk < arena_prepare_chunk_count(capacity); ++chunk)
		{
			const usz length = arena_prepare_chunk_length(capacity, chunk);
			assert(length > 0 && length <= 16 * mib);
			assert(length % (16 * 1024) == 0);
			prepared += length;
		}
		assert(prepared == capacity);
		assert(arena_prepare_chunk_length(capacity, arena_prepare_chunk_count(capacity)) == 0);
	}

	// Exercise offsets at the new ceiling without mapping physical JIT pages.
	arena_allocator large{arena_max_capacity};
	arena_range end;
	assert(large.allocate_highest(100 * mib, 16384, end));
	assert(end.offset == 924 * mib);
	assert(large.release(end.offset, end.size));
	assert(large.free_bytes() == arena_max_capacity);

	arena_allocator allocator{1024};
	arena_range low;
	arena_range high;
	assert(allocator.allocate_lowest(100, 64, low));
	assert(low.offset == 0 && low.size == 100);
	assert(allocator.allocate_highest(80, 64, high));
	assert(high.offset == 896 && high.size == 80);

	arena_range aligned;
	assert(allocator.allocate_lowest(100, 128, aligned));
	assert(aligned.offset == 128);
	assert(allocator.free_bytes() == 744);
	assert(allocator.largest_free_bytes() == 668);
	assert(!allocator.allocate_lowest(1, 3, aligned));
	assert(!allocator.release(64, 128));

	assert(allocator.release(low.offset, low.size));
	assert(allocator.release(aligned.offset, aligned.size));
	assert(allocator.release(high.offset, high.size));
	assert(allocator.free_bytes() == 1024);
	assert(allocator.largest_free_bytes() == 1024);
	assert(!allocator.release(high.offset, high.size));

	arena_range whole;
	assert(allocator.allocate_highest(1024, 1, whole));
	assert(whole.offset == 0 && allocator.free_bytes() == 0 && allocator.largest_free_bytes() == 0);
	assert(!allocator.allocate_lowest(1, 1, low));
	assert(allocator.release(whole.offset, whole.size));
	assert(allocator.free_bytes() == 1024);
	return 0;
}
