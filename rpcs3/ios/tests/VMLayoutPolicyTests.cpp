#include "../../Emu/Memory/VMLayoutPolicy.h"

#include <cstdint>

int main()
{
	using namespace vm::layout;
	static_assert(guest_address_space_size == UINT64_C(4) * 1024 * 1024 * 1024);
	static_assert(guest_memory_reservation_size == UINT64_C(8) * 1024 * 1024 * 1024);
	static_assert(exec_memory_reservation_size == UINT64_C(12) * 1024 * 1024 * 1024);
	static_assert(sparse_hook_memory_size == UINT64_C(32) * 1024 * 1024 * 1024);
	static_assert(stat_memory_reservation_size == UINT64_C(4) * 1024 * 1024 * 1024);

#ifdef RPCS3_IOS
	static_assert(!reserve_sparse_hook_memory);
	static_assert(static_reservation_size == UINT64_C(24) * 1024 * 1024 * 1024);
#else
	static_assert(reserve_sparse_hook_memory);
	static_assert(static_reservation_size == UINT64_C(56) * 1024 * 1024 * 1024);
#endif

	return 0;
}
