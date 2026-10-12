#include "Emu/RSX/Common/texture_cache_protection_policy.h"

#include <cassert>
#include <initializer_list>

int main()
{
	using namespace rsx;

#ifdef RPCS3_IOS
	assert(default_texture_protection_strategy(1) == section_protection_strategy::hash);
	assert(default_texture_protection_strategy(4096) == section_protection_strategy::hash);
	assert(default_texture_protection_strategy(16 * 1024 * 1024) == section_protection_strategy::hash);
	assert(default_texture_protection_strategy(4095, true) == section_protection_strategy::hash);
	assert(default_texture_protection_strategy(4096, true) == section_protection_strategy::lock);
	assert(default_texture_protection_strategy(16 * 1024 * 1024, true) == section_protection_strategy::lock);
#else
	assert(default_texture_protection_strategy(1) == section_protection_strategy::hash);
	assert(default_texture_protection_strategy(4095) == section_protection_strategy::hash);
	assert(default_texture_protection_strategy(4096) == section_protection_strategy::lock);
	assert(default_texture_protection_strategy(16 * 1024 * 1024) == section_protection_strategy::lock);
#endif
	for (bool requested : {false, true})
	for (bool ready : {false, true})
	for (bool detached : {false, true})
	{
		const bool permitted = allow_texture_write_tracking(requested, ready, detached);
		if (!requested || !ready || !detached) assert(!permitted);
		else assert(permitted);
	}

	assert(texture_protection_strategy_for_access(
		section_protection_strategy::hash, false) == section_protection_strategy::hash);
	assert(texture_protection_strategy_for_access(
		section_protection_strategy::hash, true) == section_protection_strategy::lock);
	assert(texture_protection_strategy_for_access(
		section_protection_strategy::lock, false) == section_protection_strategy::lock);

	return 0;
}
