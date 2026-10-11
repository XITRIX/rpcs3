# CPU compatibility is independent of the iOS deployment target. Keep the
# established modern code generation unless a legacy core is requested.
option(RPCS3_IOS_LEGACY_COMPATIBILITY "Build the ARMv8-A iOS core with optional CPU feature fallbacks" OFF)

if(RPCS3_IOS_LEGACY_COMPATIBILITY)
    if(NOT APPLE OR NOT RPCS3_FRONTEND STREQUAL "IOS" OR NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
        message(FATAL_ERROR "RPCS3_IOS_LEGACY_COMPATIBILITY is only supported by the iOS device/Simulator core")
    endif()
    if(USE_ARM_LSE2)
        message(FATAL_ERROR "RPCS3_IOS_LEGACY_COMPATIBILITY cannot assume LSE2; disable USE_ARM_LSE2")
    endif()
    add_compile_definitions(RPCS3_IOS_LEGACY_COMPATIBILITY=1)
    message(STATUS "iOS core CPU profile: legacy (ARMv8-A, runtime LSE2)")
elseif(RPCS3_FRONTEND STREQUAL "IOS" AND CMAKE_SYSTEM_NAME STREQUAL "iOS")
    message(STATUS "iOS core CPU profile: modern (ARMv8.4-A, static fast paths)")
endif()
