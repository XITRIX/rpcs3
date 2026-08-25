#pragma once

namespace rpcs3::ios
{
// Called by the upstream RSX-overlay launcher immediately before its
// shell-to-game handoff. The iOS frontend keeps RPCN alive across sessions,
// so guest signaling must be quiesced before fixed objects are rebuilt.
void prepare_big_picture_game_boot() noexcept;
}
