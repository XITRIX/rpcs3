#pragma once

// Adjacent string literals keep build_info static and preserve ABI 30.
#ifdef RPCS3_IOS_LEGACY_COMPATIBILITY
#define RPCS3_IOS_CORE_BUILD_PROFILE_JSON "\"core_variant\":\"legacy\",\"cpu_baseline\":\"armv8-a\",\"legacy_cpu_compatibility\":true,"
#else
#define RPCS3_IOS_CORE_BUILD_PROFILE_JSON "\"core_variant\":\"modern\",\"cpu_baseline\":\"armv8.4-a\",\"legacy_cpu_compatibility\":false,"
#endif
