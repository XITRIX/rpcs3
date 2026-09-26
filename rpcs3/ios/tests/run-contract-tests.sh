#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
OUTPUT_ROOT="${TMPDIR:-/tmp}/rpcs3-ios-contract-tests"
CXX_COMPILER="${CXX:-clang++}"

mkdir -p "${OUTPUT_ROOT}"

"${CXX_COMPILER}" -std=c++20 -O2 -Wall -Wextra -Werror \
    "${SCRIPT_DIR}/IOSGPUEventWaitTests.cpp" \
    -o "${OUTPUT_ROOT}/IOSGPUEventWaitTests"
"${OUTPUT_ROOT}/IOSGPUEventWaitTests"

python3 "${SCRIPT_DIR}/emit-spu-xfloat-fixture.py" "${OUTPUT_ROOT}/SPUXFloatConversion.ll"
"${CLANG:-clang}" -O3 -Wno-override-module -c "${OUTPUT_ROOT}/SPUXFloatConversion.ll" \
    -o "${OUTPUT_ROOT}/SPUXFloatConversion.o"
"${CXX_COMPILER}" -std=c++20 -O3 -Wall -Wextra -Werror \
    "${SCRIPT_DIR}/SPUXFloatConversionTests.cpp" "${OUTPUT_ROOT}/SPUXFloatConversion.o" \
    -o "${OUTPUT_ROOT}/SPUXFloatConversionTests"
"${OUTPUT_ROOT}/SPUXFloatConversionTests"

python3 "${SCRIPT_DIR}/emit-spu-batch-fixture.py" "${OUTPUT_ROOT}/SPUBatchOptimization.ll"
"${CLANG:-clang}" -O3 -Wno-override-module -c "${OUTPUT_ROOT}/SPUBatchOptimization.ll" \
    -o "${OUTPUT_ROOT}/SPUBatchOptimization.o"
"${CXX_COMPILER}" -std=c++20 -O3 -Wall -Wextra -Werror \
    "${SCRIPT_DIR}/SPUBatchOptimizationTests.cpp" "${OUTPUT_ROOT}/SPUBatchOptimization.o" \
    -o "${OUTPUT_ROOT}/SPUBatchOptimizationTests"
"${OUTPUT_ROOT}/SPUBatchOptimizationTests" quick validate

# These fixtures execute AArch64-only intrinsics from the production lowering.
case "$(uname -m)" in
    arm64|aarch64)
        python3 -B "${SCRIPT_DIR}/emit-spu-reservation-hash-fixture.py" "${OUTPUT_ROOT}/SPUReservationHashFixture.h"
        "${CXX_COMPILER}" -std=c++20 -O2 -Wall -Wextra -Werror \
            -I "${SOURCE_ROOT}" -I "${SOURCE_ROOT}/rpcs3" \
            -I "${SOURCE_ROOT}/3rdparty/asmjit/asmjit/src" -I "${OUTPUT_ROOT}" \
            "${SCRIPT_DIR}/SPUReservationHashTests.cpp" -o "${OUTPUT_ROOT}/SPUReservationHashTests"
        "${OUTPUT_ROOT}/SPUReservationHashTests"

        python3 -B "${SCRIPT_DIR}/emit-spu-int-fixture.py" "${OUTPUT_ROOT}/SPUIntOptimization.ll"
        "${CLANG:-clang}" -O3 -Wno-override-module -c "${OUTPUT_ROOT}/SPUIntOptimization.ll" \
            -o "${OUTPUT_ROOT}/SPUIntOptimization.o"
        "${CXX_COMPILER}" -std=c++20 -O3 -Wall -Wextra -Werror \
            "${SCRIPT_DIR}/SPUIntOptimizationTests.cpp" "${OUTPUT_ROOT}/SPUIntOptimization.o" \
            -o "${OUTPUT_ROOT}/SPUIntOptimizationTests"
        "${OUTPUT_ROOT}/SPUIntOptimizationTests" quick validate

        python3 -B "${SCRIPT_DIR}/emit-spu-wide-fixture.py" "${OUTPUT_ROOT}/SPUWideOptimization.ll"
        "${CLANG:-clang}" -O3 -Wno-override-module -c "${OUTPUT_ROOT}/SPUWideOptimization.ll" \
            -o "${OUTPUT_ROOT}/SPUWideOptimization.o"
        "${CXX_COMPILER}" -std=c++20 -O3 -Wall -Wextra -Werror -I "${OUTPUT_ROOT}" \
            "${SCRIPT_DIR}/SPUWideOptimizationTests.cpp" "${OUTPUT_ROOT}/SPUWideOptimization.o" \
            -o "${OUTPUT_ROOT}/SPUWideOptimizationTests"
        "${OUTPUT_ROOT}/SPUWideOptimizationTests" validate

        if [[ -n "${LLVM_CONFIG:-}" ]] || command -v llvm-config >/dev/null 2>&1; then
            python3 -B "${SCRIPT_DIR}/run-spu-arm64-lowering-tests.py"
            python3 -B "${SCRIPT_DIR}/run-spu-arm64-compare-tests.py"
        else
            echo "SPU ARM64 lowering tests not run: set LLVM_CONFIG to a native LLVM 20+ installation."
        fi
        ;;
esac

python3 "${SCRIPT_DIR}/run-silenced-fatal-log-tests.py"

"${CXX_COMPILER}" -std=c++20 -pthread -Wall -Wextra -Werror \
    "${SCRIPT_DIR}/IOSGraphicsLifecycleTests.cpp" \
    -o "${OUTPUT_ROOT}/IOSGraphicsLifecycleTests"
"${OUTPUT_ROOT}/IOSGraphicsLifecycleTests"

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
    "${SCRIPT_DIR}/RPCS3IOSGPUDefaultsTests.cpp" \
    -o "${OUTPUT_ROOT}/RPCS3IOSGPUDefaultsTests"
"${OUTPUT_ROOT}/RPCS3IOSGPUDefaultsTests"

"${CXX_COMPILER}" -std=c++20 -Wall -Wextra -Werror \
    "${SCRIPT_DIR}/RPCS3IOSSettingScopeTests.cpp" \
    -o "${OUTPUT_ROOT}/RPCS3IOSSettingScopeTests"
"${OUTPUT_ROOT}/RPCS3IOSSettingScopeTests"

"${CXX_COMPILER}" -std=c++20 -Wall -Wextra -Werror \
    "${SCRIPT_DIR}/RPCS3IOSZcullAccuracyTests.cpp" \
    -o "${OUTPUT_ROOT}/RPCS3IOSZcullAccuracyTests"
"${OUTPUT_ROOT}/RPCS3IOSZcullAccuracyTests"

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

"${CXX_COMPILER}" -std=c++20 -pthread -Wall -Wextra -Werror \
    -I "${SOURCE_ROOT}/rpcs3" \
    "${SCRIPT_DIR}/IOSAudioRecoveryTests.cpp" \
    -o "${OUTPUT_ROOT}/IOSAudioRecoveryTests"
"${OUTPUT_ROOT}/IOSAudioRecoveryTests"

"${CXX_COMPILER}" -std=c++20 -pthread -Wall -Wextra -Werror \
    -I "${SOURCE_ROOT}/rpcs3" \
    "${SCRIPT_DIR}/IOSAudioDiagnosticsTests.cpp" \
    -o "${OUTPUT_ROOT}/IOSAudioDiagnosticsTests"

bash "${SCRIPT_DIR}/run-audio-tempo-tests.sh"
"${OUTPUT_ROOT}/IOSAudioDiagnosticsTests"

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

"${CXX_COMPILER}" -std=c++20 -O2 -Wall -Wextra -Werror \
    -I "${SOURCE_ROOT}" \
    "${SCRIPT_DIR}/JITProfileTests.cpp" \
    -o "${OUTPUT_ROOT}/JITProfileTests"
"${OUTPUT_ROOT}/JITProfileTests"

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
    -I "${SOURCE_ROOT}" \
    "${SCRIPT_DIR}/TextureCacheHashTests.cpp" \
    -o "${OUTPUT_ROOT}/TextureCacheHashTests"
"${OUTPUT_ROOT}/TextureCacheHashTests"

"${CXX_COMPILER}" -std=c++20 -Wall -Wextra -Werror \
    -I "${SOURCE_ROOT}/rpcs3" \
    "${SCRIPT_DIR}/SamplerInvalidationTests.cpp" \
    -o "${OUTPUT_ROOT}/SamplerInvalidationTests"
"${OUTPUT_ROOT}/SamplerInvalidationTests"

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

"${CXX_COMPILER}" -std=c++20 -Wall -Wextra -Werror \
    -I "${SOURCE_ROOT}" -I "${SOURCE_ROOT}/rpcs3" \
    "${SCRIPT_DIR}/IOSMemoryPressurePolicyTests.cpp" \
    -o "${OUTPUT_ROOT}/IOSMemoryPressurePolicyTests"
"${OUTPUT_ROOT}/IOSMemoryPressurePolicyTests"

"${CXX_COMPILER}" -std=c++20 -Wall -Wextra -Werror \
    -I "${SOURCE_ROOT}" -I "${SOURCE_ROOT}/rpcs3" \
    "${SCRIPT_DIR}/IOSReservationLockPolicyTests.cpp" \
    -o "${OUTPUT_ROOT}/IOSReservationLockPolicyTests"
"${OUTPUT_ROOT}/IOSReservationLockPolicyTests"

"${CXX_COMPILER}" -std=c++20 -O2 -Wall -Wextra -Werror \
    -I "${SOURCE_ROOT}/rpcs3" \
    "${SCRIPT_DIR}/VMReservationRangeTests.cpp" \
    -o "${OUTPUT_ROOT}/VMReservationRangeTests"
"${OUTPUT_ROOT}/VMReservationRangeTests"

"${CXX_COMPILER}" -std=c++20 -O2 -Wall -Wextra -Werror \
    -I "${SOURCE_ROOT}" -I "${SOURCE_ROOT}/rpcs3" \
    "${SCRIPT_DIR}/IOSFPSBatchTests.cpp" "${SCRIPT_DIR}/../IOSTextureHash.cpp" \
    -o "${OUTPUT_ROOT}/IOSFPSBatchTests"
"${OUTPUT_ROOT}/IOSFPSBatchTests"

python3 -B "${SCRIPT_DIR}/emit-fragment-constant-fixture.py" "${OUTPUT_ROOT}/FragmentConstantFixture.h"
"${CXX_COMPILER}" -std=c++20 -O2 -Wall -Wextra -Werror -I "${OUTPUT_ROOT}" \
    "${SCRIPT_DIR}/FragmentConstantTests.cpp" -o "${OUTPUT_ROOT}/FragmentConstantTests"
"${OUTPUT_ROOT}/FragmentConstantTests" validate

# Execute the production NEON reservation scan only on an ARM64 host.
case "$(uname -m)" in
    arm64|aarch64)
        "${CXX_COMPILER}" -std=c++20 -O2 -Wall -Wextra -Werror \
            -I "${SOURCE_ROOT}/rpcs3" \
            "${SCRIPT_DIR}/SPUStaticShiftTests.cpp" \
            -o "${OUTPUT_ROOT}/SPUStaticShiftTests"
        "${OUTPUT_ROOT}/SPUStaticShiftTests"

        "${CXX_COMPILER}" -std=c++20 -O2 -Wall -Wextra -Werror \
            -I "${SOURCE_ROOT}/rpcs3" \
            "${SCRIPT_DIR}/SPUReservationScanTests.cpp" \
            -o "${OUTPUT_ROOT}/SPUReservationScanTests"
        "${OUTPUT_ROOT}/SPUReservationScanTests"
        ;;
esac

"${CXX_COMPILER}" -std=c++20 -O2 -Wall -Wextra -Werror \
    -I "${SOURCE_ROOT}/rpcs3" \
    "${SCRIPT_DIR}/BoundedSwizzleTests.cpp" \
    -o "${OUTPUT_ROOT}/BoundedSwizzleTests"
"${OUTPUT_ROOT}/BoundedSwizzleTests"

# Exercise the real Apple arena implementation without any executable memory.
if [[ "$(uname -s)" == "Darwin" && "$(uname -m)" == "arm64" ]]; then
    "${CXX_COMPILER}" -std=c++20 -DRPCS3_IOS \
        -I "${SOURCE_ROOT}" -I "${SOURCE_ROOT}/rpcs3" \
        "${SCRIPT_DIR}/IOSJitlessTests.cpp" "${SOURCE_ROOT}/Utilities/JITIOS.cpp" \
        -o "${OUTPUT_ROOT}/IOSJitlessTests"
    "${OUTPUT_ROOT}/IOSJitlessTests"
fi

python3 "${SCRIPT_DIR}/run-self-decryption-tests.py"

python3 "${SCRIPT_DIR}/run-arm64-memory-decoder-tests.py"

python3 "${SCRIPT_DIR}/run-texture-lookup-tests.py"
