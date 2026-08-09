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
	assert(localized_overlay_string(localized_string_id::INVALID, "en-US", "Fallback") == "Fallback");
	assert(localized_overlay_string(localized_string_id::INVALID, "en-US").empty());
	assert(localized_overlay_u32string(localized_string_id::HOME_MENU_SETTINGS, "en-US") == U"Settings");

	return 0;
}
