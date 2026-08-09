#include "../../../Utilities/JITArenaAllocator.h"
#include "../../../Utilities/JITIOSLayoutPolicy.h"

#include <cassert>

int main()
{
	using namespace rpcs3::ios::jit;

	static_assert(choose_arena_capacity(0) == 384 * mib);
	static_assert(choose_arena_capacity(4ull * 1024 * mib) == 256 * mib);
	static_assert(choose_arena_capacity(6ull * 1024 * mib) == 384 * mib);
	static_assert(choose_arena_capacity(8ull * 1024 * mib) == 512 * mib);

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
	assert(!allocator.allocate_lowest(1, 3, aligned));
	assert(!allocator.release(64, 128));

	assert(allocator.release(low.offset, low.size));
	assert(allocator.release(aligned.offset, aligned.size));
	assert(allocator.release(high.offset, high.size));
	assert(allocator.free_bytes() == 1024);
	assert(!allocator.release(high.offset, high.size));

	arena_range whole;
	assert(allocator.allocate_highest(1024, 1, whole));
	assert(whole.offset == 0 && allocator.free_bytes() == 0);
	assert(!allocator.allocate_lowest(1, 1, low));
	assert(allocator.release(whole.offset, whole.size));
	assert(allocator.free_bytes() == 1024);
	return 0;
}
