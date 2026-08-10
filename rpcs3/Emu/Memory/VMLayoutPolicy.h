#pragma once

#include <cstdint>

namespace vm::layout
{
	inline constexpr std::uint64_t guest_address_space_size = 0x1'0000'0000;
	inline constexpr std::uint64_t guest_memory_reservation_size = 2 * guest_address_space_size;
	inline constexpr std::uint64_t exec_memory_reservation_size = 3 * guest_address_space_size;
	inline constexpr std::uint64_t sparse_hook_memory_size = 8 * guest_address_space_size;
	inline constexpr std::uint64_t stat_memory_reservation_size = guest_address_space_size;

#ifdef RPCS3_IOS
	// The sparse hook mapping is a desktop overcommit workaround and is never
	// addressed by guest-memory or JIT code. Reserving it consumes another 32 GiB
	// of virtual address space, which exceeds the mapping budget on iOS 26.
	inline constexpr bool reserve_sparse_hook_memory = false;
#else
	inline constexpr bool reserve_sparse_hook_memory = true;
#endif

	inline constexpr std::uint64_t static_reservation_size =
		guest_memory_reservation_size + exec_memory_reservation_size +
		(reserve_sparse_hook_memory ? sparse_hook_memory_size : 0) +
		stat_memory_reservation_size;
}
