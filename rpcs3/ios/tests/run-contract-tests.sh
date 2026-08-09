#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
OUTPUT_ROOT="${TMPDIR:-/tmp}/rpcs3-ios-contract-tests"
CXX_COMPILER="${CXX:-clang++}"

mkdir -p "${OUTPUT_ROOT}"

"${CXX_COMPILER}" -std=c++20 -Wall -Wextra -Werror \
    "${SCRIPT_DIR}/RPCS3IOSContractTests.cpp" \
    -o "${OUTPUT_ROOT}/RPCS3IOSContractTests"
"${OUTPUT_ROOT}/RPCS3IOSContractTests"

"${CXX_COMPILER}" -std=c++20 -Wall -Wextra -Werror \
    "${SCRIPT_DIR}/RPCS3IOSInputTests.cpp" \
    -o "${OUTPUT_ROOT}/RPCS3IOSInputTests"
"${OUTPUT_ROOT}/RPCS3IOSInputTests"

"${CXX_COMPILER}" -std=c++20 -Wall -Wextra -Werror \
    "${SCRIPT_DIR}/SharedMemoryBackingTests.cpp" \
    -o "${OUTPUT_ROOT}/SharedMemoryBackingTests"
"${OUTPUT_ROOT}/SharedMemoryBackingTests"

"${CXX_COMPILER}" -std=c++20 -Wall -Wextra -Werror \
    -I "${SOURCE_ROOT}" -I "${SOURCE_ROOT}/rpcs3" \
    "${SCRIPT_DIR}/JITArenaAllocatorTests.cpp" \
    -o "${OUTPUT_ROOT}/JITArenaAllocatorTests"
"${OUTPUT_ROOT}/JITArenaAllocatorTests"

"${CXX_COMPILER}" -std=c++20 -Wall -Wextra -Werror \
    -I "${SOURCE_ROOT}" -I "${SOURCE_ROOT}/rpcs3" \
    "${SCRIPT_DIR}/JITUniversalProtocolTests.cpp" \
    -o "${OUTPUT_ROOT}/JITUniversalProtocolTests"
"${OUTPUT_ROOT}/JITUniversalProtocolTests"

"${CXX_COMPILER}" -std=c++20 -Wall -Wextra -Werror \
    -I "${SOURCE_ROOT}" -I "${SOURCE_ROOT}/rpcs3" \
    "${SCRIPT_DIR}/TextureCacheProtectionPolicyTests.cpp" \
    -o "${OUTPUT_ROOT}/TextureCacheProtectionPolicyHostTests"
"${OUTPUT_ROOT}/TextureCacheProtectionPolicyHostTests"

"${CXX_COMPILER}" -std=c++20 -Wall -Wextra -Werror \
    -DRPCS3_IOS=1 \
    -I "${SOURCE_ROOT}" -I "${SOURCE_ROOT}/rpcs3" \
    "${SCRIPT_DIR}/TextureCacheProtectionPolicyTests.cpp" \
    -o "${OUTPUT_ROOT}/TextureCacheProtectionPolicyIOSTests"
"${OUTPUT_ROOT}/TextureCacheProtectionPolicyIOSTests"
