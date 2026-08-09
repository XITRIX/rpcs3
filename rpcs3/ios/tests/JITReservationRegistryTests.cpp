#include "../../../Utilities/JITReservationRegistry.h"

#include <cassert>
#include <limits>

int main()
{
	using rpcs3::ios::jit::reservation_registry;

	reservation_registry registry;
	assert(registry.insert(0x10000000, 0x20000000));
	assert(registry.size() == 1);
	assert(registry.contains(0x10000000, 0x20000000));
	assert(registry.contains(0x10004000, 0x80000));
	assert(!registry.contains(0x0ffff000, 0x2000));
	assert(!registry.contains(0x2ffff000, 0x2000));

	assert(!registry.insert(0x18000000, 0x1000));
	assert(!registry.insert(0, 0x1000));
	assert(!registry.insert(std::numeric_limits<uptr>::max() - 0x10, 0x20));
	assert(registry.insert(0x40000000, 0x10000000));
	assert(registry.size() == 2);

	registry.remove_contained(0x10000000, 0x20000000);
	assert(!registry.contains(0x10000000, 1));
	assert(registry.contains(0x40000000, 1));
	assert(registry.size() == 1);
	return 0;
}
