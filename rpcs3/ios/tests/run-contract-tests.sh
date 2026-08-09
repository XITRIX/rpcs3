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
    "${SCRIPT_DIR}/SharedMemoryBackingTests.cpp" \
    -o "${OUTPUT_ROOT}/SharedMemoryBackingTests"
"${OUTPUT_ROOT}/SharedMemoryBackingTests"

"${CXX_COMPILER}" -std=c++20 -Wall -Wextra -Werror \
    -I "${SOURCE_ROOT}" -I "${SOURCE_ROOT}/rpcs3" \
    "${SCRIPT_DIR}/JITAliasRegistryTests.cpp" \
    -o "${OUTPUT_ROOT}/JITAliasRegistryTests"
"${OUTPUT_ROOT}/JITAliasRegistryTests"

"${CXX_COMPILER}" -std=c++20 -Wall -Wextra -Werror \
    -I "${SOURCE_ROOT}" -I "${SOURCE_ROOT}/rpcs3" \
    "${SCRIPT_DIR}/JITUniversalProtocolTests.cpp" \
    -o "${OUTPUT_ROOT}/JITUniversalProtocolTests"
"${OUTPUT_ROOT}/JITUniversalProtocolTests"
