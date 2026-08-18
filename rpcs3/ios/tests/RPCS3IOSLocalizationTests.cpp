#include "../RPCS3IOSLocalization.h"

#include "Emu/localized_string_id.h"

#include <cassert>
#include <string>

namespace
{
std::string test_localization_resolver(
	std::string_view language_tag,
	std::string_view localization_key,
	std::string_view english_value)
{
	if (language_tag == "ru-RU" && localization_key == "core.HOME_MENU_TITLE")
	{
		return "Главное меню";
	}
	if (language_tag == "ru-RU" && localization_key == "setting.9b1804f454206d48")
	{
		return "Приблизительно";
	}
	if (language_tag == "ru-RU" && localization_key == "core.RSX_OVERLAYS_TROPHY_BRONZE")
	{
		return "Получен бронзовый приз.\n%0";
	}
	return std::string{english_value};
}
}

int main()
{
	using namespace rpcs3::ios;

	assert(localized_overlay_string(localized_string_id::HOME_MENU_TITLE, "en-US") == "Home Menu");
	assert(localized_overlay_string(localized_string_id::CELL_SAVEDATA_SAVE, "en-US", "Slot 1") == "Save this data?\n\nSlot 1");
	for (int raw = static_cast<int>(localized_string_id::INVALID);
		raw <= static_cast<int>(localized_string_id::SAVESTATE_FAILED_DUE_TO_MISSING_SPU_SETTING);
		raw++)
	{
		const auto id = static_cast<localized_string_id>(raw);
		const std::string text = localized_overlay_string(id, "en-US");
		assert(id == localized_string_id::RSX_OVERLAYS_SPINNER_NO_TEXT || !text.empty());
	}
	assert(localized_overlay_string(
		localized_string_id::HOME_MENU_FRIENDS_BLOCK_USER_MSG,
		"en-GB",
		"Player") == "Block this user?\n\nPlayer");
	assert(localized_overlay_string(
		localized_string_id::HOME_MENU_TROPHY_LIST_TITLE,
		"en-US",
		"25%") == "Trophy Progress: 25%");
	assert(localized_overlay_string(
		localized_string_id::HOME_MENU_TROPHY_SYNC_TROPHIES,
		"en-US") == "Sync trophies");
	assert(localized_overlay_string(
		localized_string_id::HOME_MENU_TROPHY_SYNCING_TROPHIES,
		"en-US") == "Syncing...");
	assert(localized_overlay_string(
		localized_string_id::HOME_MENU_TROPHY_SYNC_SUCCESS,
		"en-US") == "Synced!");
	assert(localized_overlay_string(
		localized_string_id::HOME_MENU_TROPHY_SYNC_FAILED,
		"en-US") == "Sync failed");
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
	assert(localized_overlay_string(localized_string_id::INVALID, "en-US") == "Invalid");
	assert(localized_overlay_u32string(localized_string_id::HOME_MENU_SETTINGS, "en-US") == U"Settings");
	assert(localized_setting_string("VSync Mode", 1, "Adaptive", "en-US") == "Adaptive");

	set_localization_resolver(&test_localization_resolver);
	assert(localized_overlay_string(localized_string_id::HOME_MENU_TITLE, "ru-RU") == "Главное меню");
	assert(localized_overlay_string(localized_string_id::RSX_OVERLAYS_TROPHY_BRONZE, "ru-RU", "Готово") == "Получен бронзовый приз.\nГотово");
	assert(localized_setting_string("VSync Mode", 1, "Adaptive", "ru-RU") == "Приблизительно");
	set_localization_resolver(nullptr);
	assert(localized_overlay_string(localized_string_id::HOME_MENU_TITLE, "ru-RU") == "Home Menu");

	return 0;
}
