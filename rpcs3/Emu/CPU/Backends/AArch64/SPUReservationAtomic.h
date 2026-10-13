#pragma once

#include <type_traits>

namespace rpcs3::spu
{
// Keep the language-level strong, sequentially consistent CAS. For the hot
// Apple reservation stamp, LL/SC avoids the cache-line traffic of failed LSE
// writes when multiple SPUs compete for the same stamp. Let Clang generate the
// exclusive loop, including retry and monitor clearing on comparison failure.
#if defined(__APPLE__) && defined(__aarch64__) && defined(__clang__)
#define RPCS3_SPU_RESERVATION_CAS __attribute__((target("no-lse"), noinline))
#else
#define RPCS3_SPU_RESERVATION_CAS
#endif

template <typename T>
RPCS3_SPU_RESERVATION_CAS inline bool reservation_compare_exchange(T& stamp, T expected, T desired)
{
    static_assert(std::is_unsigned_v<T> && sizeof(T) == 8);
    return __atomic_compare_exchange_n(&stamp, &expected, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

#undef RPCS3_SPU_RESERVATION_CAS
}
