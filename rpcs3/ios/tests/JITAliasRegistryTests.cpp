#include "../../../Utilities/JITAliasRegistry.h"

#include <cassert>

int main()
{
	using rpcs3::ios::jit::alias_registry;

	alias_registry registry;
	assert(registry.insert(0x1000, 0x9000, 0x1000));
	assert(registry.size() == 1);
	assert(registry.contains(0x1000, 1));
	assert(registry.contains(0x1800, 0x800));
	assert(!registry.contains(0x1800, 0x801));
	assert(registry.translate(0x1120, 0x20) == reinterpret_cast<void*>(0x9120));
	assert(!registry.insert(0x1800, 0xa000, 0x1000));
	assert(!registry.insert(0, 0xa000, 0x1000));

	unsigned removed = 0;
	registry.remove_contained(0x1000, 0x1000, [&](const auto& item)
	{
		assert(item.writable == 0x9000);
		++removed;
	});
	assert(removed == 1);
	assert(registry.size() == 0);
	assert(registry.translate(0x1000, 1) == nullptr);
	return 0;
}
