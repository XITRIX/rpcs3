#include "../RPCS3IOSLocalization.h"

#include "Emu/localized_string_id.h"

#include <cassert>

int main()
{
	using namespace rpcs3::ios;

	assert(localized_overlay_string(localized_string_id::HOME_MENU_TITLE, "en-US") == "Home Menu");
	assert(localized_overlay_string(localized_string_id::HOME_MENU_RESUME, "ru-RU") == "Resume Game");
	assert(localized_overlay_string(
		localized_string_id::HOME_MENU_FRIENDS_BLOCK_USER_MSG,
		"en-GB",
		"Player") == "Block this user?\n\nPlayer");
	assert(localized_overlay_string(
		localized_string_id::HOME_MENU_TROPHY_LIST_TITLE,
		"en-US",
		"25%") == "Trophy Progress: 25%");
	assert(localized_overlay_string(
		localized_string_id::SAVESTATE_FAILED_DUE_TO_VDEC,
		"en-US") == "SaveState failed: a video or cutscene is active. Wait for it to finish and try again.");
	assert(localized_overlay_string(
		localized_string_id::SAVESTATE_FAILED_DUE_TO_SAVEDATA,
		"en-US") == "SaveState failed: the game is saving data. Wait for it to finish and try again.");
	assert(localized_overlay_string(
		localized_string_id::SAVESTATE_FAILED_DUE_TO_SPU,
		"en-US") == "SaveState failed: RPCS3 could not safely lock the SPU state.");
	assert(localized_overlay_string(
		localized_string_id::SAVESTATE_FAILED_DUE_TO_MISSING_SPU_SETTING,
		"en-US") == "SaveState failed: enable Advanced > Compatible Savestate Mode, restart the game, and try again. This can reduce performance.");
	assert(localized_overlay_string(localized_string_id::INVALID, "en-US", "Fallback") == "Fallback");
	assert(localized_overlay_string(localized_string_id::INVALID, "en-US").empty());
	assert(localized_overlay_u32string(localized_string_id::HOME_MENU_SETTINGS, "en-US") == U"Settings");

	return 0;
}
