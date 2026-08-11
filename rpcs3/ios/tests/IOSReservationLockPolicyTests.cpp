#include "ios/IOSReservationLockPolicy.h"

#include <cassert>

int main()
{
	using rpcs3::ios::reservation_lock_should_yield;

	static_assert(!reservation_lock_should_yield(0));
	static_assert(!reservation_lock_should_yield(127));
	static_assert(reservation_lock_should_yield(128));
	static_assert(!reservation_lock_should_yield(129));
	static_assert(!reservation_lock_should_yield(159));
	static_assert(reservation_lock_should_yield(160));
	static_assert(reservation_lock_should_yield(1024));

	std::uint64_t yields = 0;
	for (std::uint64_t iteration = 0; iteration < 1024; ++iteration)
	{
		yields += reservation_lock_should_yield(iteration);
	}

	assert(yields == 28);
}
