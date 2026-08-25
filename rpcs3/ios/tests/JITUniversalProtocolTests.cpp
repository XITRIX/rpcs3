#include "../../../Utilities/JITIOS.h"
#include "../../../Utilities/JITIOSLayoutPolicy.h"

#include <cassert>

int main()
{
	static_assert(rpcs3::ios::jit::breakpoint_immediate == 0xf00d);
	static_assert(rpcs3::ios::jit::command_detach == 0);
	static_assert(rpcs3::ios::jit::command_prepare_region == 1);
	static_assert(rpcs3::ios::jit::universal_backend_ios_major == 26);
	static_assert(rpcs3::ios::jit::backend_for_ios_major(17) == rpcs3::ios::jit::arena_backend::legacy_debugger);
	static_assert(rpcs3::ios::jit::backend_for_ios_major(18) == rpcs3::ios::jit::arena_backend::legacy_debugger);
	static_assert(rpcs3::ios::jit::backend_for_ios_major(26) == rpcs3::ios::jit::arena_backend::universal_mirrored);
	static_assert(rpcs3::ios::jit::backend_for_ios_major(27) == rpcs3::ios::jit::arena_backend::universal_mirrored);
	static_assert(rpcs3::ios::jit::arena_prepare_chunk_size == 16 * rpcs3::ios::jit::mib);
	static_assert(rpcs3::ios::jit::arena_prepare_chunk_count(448 * rpcs3::ios::jit::mib) == 28);
	static_assert(rpcs3::ios::jit::arena_prepare_chunk_count(512 * rpcs3::ios::jit::mib) == 32);

	assert(rpcs3::ios::jit::breakpoint_immediate == 0xf00d);
	assert(rpcs3::ios::jit::command_detach == 0);
	assert(rpcs3::ios::jit::command_prepare_region == 1);
	assert(rpcs3::ios::jit::backend_for_ios_major(18) == rpcs3::ios::jit::arena_backend::legacy_debugger);
	assert(rpcs3::ios::jit::backend_for_ios_major(26) == rpcs3::ios::jit::arena_backend::universal_mirrored);
	assert(rpcs3::ios::jit::arena_prepare_chunk_count(0) == 0);
	assert(rpcs3::ios::jit::arena_prepare_chunk_count(17 * rpcs3::ios::jit::mib) == 2);
	assert(rpcs3::ios::jit::arena_prepare_chunk_length(17 * rpcs3::ios::jit::mib, 0) == 16 * rpcs3::ios::jit::mib);
	assert(rpcs3::ios::jit::arena_prepare_chunk_length(17 * rpcs3::ios::jit::mib, 1) == rpcs3::ios::jit::mib);
	return 0;
}
