#include "../../../Utilities/JITIOS.h"

#include <cassert>

int main()
{
	static_assert(rpcs3::ios::jit::breakpoint_immediate == 0xf00d);
	static_assert(rpcs3::ios::jit::command_prepare_region == 1);

	assert(rpcs3::ios::jit::breakpoint_immediate == 0xf00d);
	assert(rpcs3::ios::jit::command_prepare_region == 1);
	return 0;
}
