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
    "${SCRIPT_DIR}/RPCS3IOSPathTests.cpp" \
    -o "${OUTPUT_ROOT}/RPCS3IOSPathTests"
"${OUTPUT_ROOT}/RPCS3IOSPathTests"

"${CXX_COMPILER}" -std=c++20 -Wall -Wextra -Werror \
    -I "${SOURCE_ROOT}" -I "${SOURCE_ROOT}/rpcs3" \
    "${SCRIPT_DIR}/RPCS3IOSResolutionTests.cpp" \
    -o "${OUTPUT_ROOT}/RPCS3IOSResolutionTests"
"${OUTPUT_ROOT}/RPCS3IOSResolutionTests"

"${CXX_COMPILER}" -std=c++20 -Wall -Wextra -Werror \
    -I "${SOURCE_ROOT}" -I "${SOURCE_ROOT}/rpcs3" \
    "${SCRIPT_DIR}/RPCS3IOSLocalizationTests.cpp" \
    "${SCRIPT_DIR}/../RPCS3IOSLocalization.cpp" \
    -o "${OUTPUT_ROOT}/RPCS3IOSLocalizationTests"
"${OUTPUT_ROOT}/RPCS3IOSLocalizationTests"

"${CXX_COMPILER}" -std=c++20 -Wall -Wextra -Werror \
    -I "${SOURCE_ROOT}" -I "${SOURCE_ROOT}/rpcs3" \
    "${SCRIPT_DIR}/GameArchiveContractTests.cpp" \
    -o "${OUTPUT_ROOT}/GameArchiveContractTests"
"${OUTPUT_ROOT}/GameArchiveContractTests"

"${CXX_COMPILER}" -std=c++20 -Wall -Wextra -Werror \
    "${SCRIPT_DIR}/RPCS3IOSInputTests.cpp" \
    -o "${OUTPUT_ROOT}/RPCS3IOSInputTests"
"${OUTPUT_ROOT}/RPCS3IOSInputTests"

"${CXX_COMPILER}" -std=c++20 -Wall -Wextra -Werror \
    -I "${SOURCE_ROOT}" -I "${SOURCE_ROOT}/rpcs3" \
    "${SCRIPT_DIR}/IOSAudioBufferContractTests.cpp" \
    -o "${OUTPUT_ROOT}/IOSAudioBufferContractTests"
"${OUTPUT_ROOT}/IOSAudioBufferContractTests"

"${CXX_COMPILER}" -std=c++20 -Wall -Wextra -Werror \
    -I "${SOURCE_ROOT}" -I "${SOURCE_ROOT}/rpcs3" \
    "${SCRIPT_DIR}/RPCS3IOSOverlayMediaTests.cpp" \
    -o "${OUTPUT_ROOT}/RPCS3IOSOverlayMediaTests"
"${OUTPUT_ROOT}/RPCS3IOSOverlayMediaTests"

"${CXX_COMPILER}" -std=c++20 -Wall -Wextra -Werror \
    "${SCRIPT_DIR}/SharedMemoryBackingTests.cpp" \
    -o "${OUTPUT_ROOT}/SharedMemoryBackingTests"
"${OUTPUT_ROOT}/SharedMemoryBackingTests"

"${CXX_COMPILER}" -std=c++20 -Wall -Wextra -Werror \
    -I "${SOURCE_ROOT}" -I "${SOURCE_ROOT}/rpcs3" \
    "${SCRIPT_DIR}/VMLayoutPolicyTests.cpp" \
    -o "${OUTPUT_ROOT}/VMLayoutPolicyHostTests"
"${OUTPUT_ROOT}/VMLayoutPolicyHostTests"

"${CXX_COMPILER}" -std=c++20 -Wall -Wextra -Werror \
    -DRPCS3_IOS=1 \
    -I "${SOURCE_ROOT}" -I "${SOURCE_ROOT}/rpcs3" \
    "${SCRIPT_DIR}/VMLayoutPolicyTests.cpp" \
    -o "${OUTPUT_ROOT}/VMLayoutPolicyIOSTests"
"${OUTPUT_ROOT}/VMLayoutPolicyIOSTests"

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

"${CXX_COMPILER}" -std=c++20 -Wall -Wextra -Werror \
    -I "${SOURCE_ROOT}" -I "${SOURCE_ROOT}/rpcs3" \
    "${SCRIPT_DIR}/VRAMBudgetPolicyTests.cpp" \
    -o "${OUTPUT_ROOT}/VRAMBudgetPolicyHostTests"
"${OUTPUT_ROOT}/VRAMBudgetPolicyHostTests"

"${CXX_COMPILER}" -std=c++20 -Wall -Wextra -Werror \
    -DRPCS3_IOS=1 \
    -I "${SOURCE_ROOT}" -I "${SOURCE_ROOT}/rpcs3" \
    "${SCRIPT_DIR}/VRAMBudgetPolicyTests.cpp" \
    -o "${OUTPUT_ROOT}/VRAMBudgetPolicyIOSTests"
"${OUTPUT_ROOT}/VRAMBudgetPolicyIOSTests"
