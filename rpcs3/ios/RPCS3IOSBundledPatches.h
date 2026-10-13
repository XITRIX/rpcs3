#pragma once

#include <string_view>

namespace rpcs3::ios
{
// Adapted from ARMSX3 39023e231226 / cff5ef467641, jpolo1224, GPL-2.0-only.
// Parsed by RPCS3's native patch engine for both boot and the patch manager.
// Optional: enabling this never changes the user's SPU accuracy setting.
// BLES01128 and the standalone EFLC executable are not verified by the source.
inline constexpr std::string_view bundled_patches = R"patch(Version: 1.2
SPU-d0a1508208613e2184fbd64906d34d5f07890033:
  "ARMSX3 Approximate collision fix":
    Games:
      "Grand Theft Auto IV":
        BLUS30682: [ "01.02" ]
        BLUS30127: [ "01.08" ]
        NPUB30702: [ "01.08" ]
    Author: "jpolo1224"
    Notes: >-
      Allows testing SPU XFloat Accuracy: Approximate without falling through
      the world. Changes fourteen collision-search initializers from SPU max
      to FLT_MAX; requires the exact collision executable hash. Verified by
      ARMSX3 for US Complete Edition 01.02 (including its episodes) and US GTA IV
      01.08. European Complete Edition and standalone Episodes from Liberty City
      are unverified. Enable this patch before selecting Approximate and cold
      boot the game; keep Accurate if the executable does not match. Apple
      gameplay and collision behavior remain unvalidated.
    Patch Version: 1.0
    Patch:
      - [ be32, 0x0c4f4, 0x413fbfd4 ]
      - [ be32, 0x0e3b0, 0x413fbf90 ]
      - [ be32, 0x0eca0, 0x413fbfa3 ]
      - [ be32, 0x0f630, 0x413fbf82 ]
      - [ be32, 0x0fa38, 0x413fbf91 ]
      - [ be32, 0x10e30, 0x413fbf9b ]
      - [ be32, 0x11470, 0x413fbfde ]
      - [ be32, 0x11508, 0x413fbfc4 ]
      - [ be32, 0x154b0, 0x413fbfbc ]
      - [ be32, 0x15528, 0x413fbfb2 ]
      - [ be32, 0x157a0, 0x413fbfcb ]
      - [ be32, 0x158d8, 0x413fbfa0 ]
      - [ be32, 0x17098, 0x413fbfbd ]
      - [ be32, 0x175f8, 0x413fbfcf ]
)patch";
}
