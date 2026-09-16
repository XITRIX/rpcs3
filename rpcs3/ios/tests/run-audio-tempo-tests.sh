#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
OUTPUT_ROOT="${AUDIO_TEST_OUTPUT_ROOT:-${TMPDIR:-/tmp}/rpcs3-ios-contract-tests}"
SOUNDTOUCH_ROOT="${SOURCE_ROOT}/3rdparty/soundtouch/soundtouch"
CXX_COMPILER="${CXX:-clang++}"
mkdir -p "${OUTPUT_ROOT}"

# Match the core's bundled SoundTouch source list and public definitions.
# Sanitizer flags can be passed without changing the production build.
EXTRA_FLAGS=(-g)
if [[ "${AUDIO_TEST_SANITIZERS:-0}" == 1 ]]; then
    EXTRA_FLAGS=(-g -fsanitize=address,undefined -fno-omit-frame-pointer)
fi
SOUNDTOUCH_SOURCES=()
for source in AAFilter FIFOSampleBuffer FIRFilter InterpolateCubic InterpolateLinear InterpolateShannon RateTransposer SoundTouch sse_optimized TDStretch; do
    SOUNDTOUCH_SOURCES+=("${SOUNDTOUCH_ROOT}/source/SoundTouch/${source}.cpp")
done
"${CXX_COMPILER}" -std=c++20 -O2 -Wall -Wextra \
    "${EXTRA_FLAGS[@]}" \
    -DST_NO_EXCEPTION_HANDLING -DUSE_MULTICH_ALWAYS -DSOUNDTOUCH_FLOAT_SAMPLES \
    -I "${SOURCE_ROOT}/rpcs3" -I "${SOUNDTOUCH_ROOT}/include" \
    "${SCRIPT_DIR}/IOSAudioTempoTests.cpp" "${SOUNDTOUCH_SOURCES[@]}" \
    -o "${OUTPUT_ROOT}/IOSAudioTempoTests"
"${OUTPUT_ROOT}/IOSAudioTempoTests"
