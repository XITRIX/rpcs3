#include "RPCS3IOSLocalization.h"

#include "Emu/localized_string_id.h"

#include <atomic>

namespace rpcs3::ios
{
namespace
{
using id = localized_string_id;

std::atomic<localization_resolver> g_localization_resolver{nullptr};

struct localized_source
{
	std::string_view key;
	std::string_view english;
};

std::string setting_localization_key(
	std::string_view setting_name,
	std::uint32_t enum_index,
	std::string_view value)
{
	constexpr std::uint64_t offset_basis = 14695981039346656037ull;
	constexpr std::uint64_t prime = 1099511628211ull;
	std::uint64_t hash = offset_basis;
	const auto append = [&hash](std::uint8_t byte)
	{
		hash = (hash ^ byte) * prime;
	};
	for (const unsigned char byte : setting_name)
	{
		append(byte);
	}
	append(0);
	for (unsigned shift = 0; shift < 32; shift += 8)
	{
		append(static_cast<std::uint8_t>(enum_index >> shift));
	}
	append(0);
	for (const unsigned char byte : value)
	{
		append(byte);
	}

	constexpr char hex[] = "0123456789abcdef";
	std::string key = "setting.";
	key.resize(key.size() + 16);
	for (std::size_t index = 0; index < 16; index++)
	{
		key[8 + index] = hex[(hash >> ((15 - index) * 4)) & 0xf];
	}
	return key;
}

localized_source overlay_source(id value) noexcept
{
	switch (value)
	{
	case id::INVALID: return {"core.INVALID", "Invalid"};
	case id::RSX_OVERLAYS_SPINNER_NO_TEXT: return {};
	case id::RSX_OVERLAYS_TROPHY_BRONZE: return {"core.RSX_OVERLAYS_TROPHY_BRONZE", "You have earned a bronze trophy.\n%0"};
	case id::RSX_OVERLAYS_TROPHY_SILVER: return {"core.RSX_OVERLAYS_TROPHY_SILVER", "You have earned a silver trophy.\n%0"};
	case id::RSX_OVERLAYS_TROPHY_GOLD: return {"core.RSX_OVERLAYS_TROPHY_GOLD", "You have earned a gold trophy.\n%0"};
	case id::RSX_OVERLAYS_TROPHY_PLATINUM: return {"core.RSX_OVERLAYS_TROPHY_PLATINUM", "You have earned a platinum trophy.\n%0"};
	case id::RSX_OVERLAYS_COMPILING_SHADERS: return {"core.RSX_OVERLAYS_COMPILING_SHADERS", "Compiling shaders"};
	case id::RSX_OVERLAYS_COMPILING_PPU_MODULES: return {"core.RSX_OVERLAYS_COMPILING_PPU_MODULES", "Compiling PPU Modules"};
	case id::RSX_OVERLAYS_MSG_DIALOG_YES: return {"core.RSX_OVERLAYS_MSG_DIALOG_YES", "Yes"};
	case id::RSX_OVERLAYS_MSG_DIALOG_NO: return {"core.RSX_OVERLAYS_MSG_DIALOG_NO", "No"};
	case id::RSX_OVERLAYS_MSG_DIALOG_CANCEL: return {"core.RSX_OVERLAYS_MSG_DIALOG_CANCEL", "Back"};
	case id::RSX_OVERLAYS_MSG_DIALOG_OK: return {"core.RSX_OVERLAYS_MSG_DIALOG_OK", "OK"};
	case id::RSX_OVERLAYS_SAVE_DIALOG_TITLE: return {"core.RSX_OVERLAYS_SAVE_DIALOG_TITLE", "Save Dialog"};
	case id::RSX_OVERLAYS_SAVE_DIALOG_DELETE: return {"core.RSX_OVERLAYS_SAVE_DIALOG_DELETE", "Delete Save"};
	case id::RSX_OVERLAYS_SAVE_DIALOG_LOAD: return {"core.RSX_OVERLAYS_SAVE_DIALOG_LOAD", "Load Save"};
	case id::RSX_OVERLAYS_SAVE_DIALOG_SAVE: return {"core.RSX_OVERLAYS_SAVE_DIALOG_SAVE", "Save"};
	case id::RSX_OVERLAYS_OSK_DIALOG_ACCEPT: return {"core.RSX_OVERLAYS_OSK_DIALOG_ACCEPT", "Enter"};
	case id::RSX_OVERLAYS_OSK_DIALOG_CANCEL: return {"core.RSX_OVERLAYS_OSK_DIALOG_CANCEL", "Back"};
	case id::RSX_OVERLAYS_OSK_DIALOG_SPACE: return {"core.RSX_OVERLAYS_OSK_DIALOG_SPACE", "Space"};
	case id::RSX_OVERLAYS_OSK_DIALOG_BACKSPACE: return {"core.RSX_OVERLAYS_OSK_DIALOG_BACKSPACE", "Backspace"};
	case id::RSX_OVERLAYS_OSK_DIALOG_SHIFT: return {"core.RSX_OVERLAYS_OSK_DIALOG_SHIFT", "Shift"};
	case id::RSX_OVERLAYS_OSK_DIALOG_ENTER_TEXT: return {"core.RSX_OVERLAYS_OSK_DIALOG_ENTER_TEXT", "[Enter Text]"};
	case id::RSX_OVERLAYS_OSK_DIALOG_ENTER_PASSWORD: return {"core.RSX_OVERLAYS_OSK_DIALOG_ENTER_PASSWORD", "[Enter Password]"};
	case id::RSX_OVERLAYS_MEDIA_DIALOG_TITLE: return {"core.RSX_OVERLAYS_MEDIA_DIALOG_TITLE", "Select media"};
	case id::RSX_OVERLAYS_MEDIA_DIALOG_TITLE_PHOTO_IMPORT: return {"core.RSX_OVERLAYS_MEDIA_DIALOG_TITLE_PHOTO_IMPORT", "Select photo to import"};
	case id::RSX_OVERLAYS_MEDIA_DIALOG_EMPTY: return {"core.RSX_OVERLAYS_MEDIA_DIALOG_EMPTY", "No media found."};
	case id::RSX_OVERLAYS_LIST_SELECT: return {"core.RSX_OVERLAYS_LIST_SELECT", "Enter"};
	case id::RSX_OVERLAYS_LIST_CANCEL: return {"core.RSX_OVERLAYS_LIST_CANCEL", "Back"};
	case id::RSX_OVERLAYS_LIST_DENY: return {"core.RSX_OVERLAYS_LIST_DENY", "Deny"};
	case id::RSX_OVERLAYS_PRESSURE_INTENSITY_TOGGLED_OFF: return {"core.RSX_OVERLAYS_PRESSURE_INTENSITY_TOGGLED_OFF", "Pressure intensity mode of player %0 disabled"};
	case id::RSX_OVERLAYS_PRESSURE_INTENSITY_TOGGLED_ON: return {"core.RSX_OVERLAYS_PRESSURE_INTENSITY_TOGGLED_ON", "Pressure intensity mode of player %0 enabled"};
	case id::RSX_OVERLAYS_ANALOG_LIMITER_TOGGLED_OFF: return {"core.RSX_OVERLAYS_ANALOG_LIMITER_TOGGLED_OFF", "Analog limiter of player %0 disabled"};
	case id::RSX_OVERLAYS_ANALOG_LIMITER_TOGGLED_ON: return {"core.RSX_OVERLAYS_ANALOG_LIMITER_TOGGLED_ON", "Analog limiter of player %0 enabled"};
	case id::RSX_OVERLAYS_MOUSE_AND_KEYBOARD_EMULATED: return {"core.RSX_OVERLAYS_MOUSE_AND_KEYBOARD_EMULATED", "Mouse and keyboard are now used as emulated devices."};
	case id::RSX_OVERLAYS_MOUSE_AND_KEYBOARD_PAD: return {"core.RSX_OVERLAYS_MOUSE_AND_KEYBOARD_PAD", "Mouse and keyboard are now used as pad."};

	case id::CELL_GAME_ERROR_BROKEN_GAMEDATA: return {"core.CELL_GAME_ERROR_BROKEN_GAMEDATA", "ERROR: Game data is corrupted. The application will continue."};
	case id::CELL_GAME_ERROR_BROKEN_HDDGAME: return {"core.CELL_GAME_ERROR_BROKEN_HDDGAME", "ERROR: HDD boot game is corrupted. The application will continue."};
	case id::CELL_GAME_ERROR_BROKEN_EXIT_GAMEDATA: return {"core.CELL_GAME_ERROR_BROKEN_EXIT_GAMEDATA", "ERROR: Game data is corrupted. The application will be terminated."};
	case id::CELL_GAME_ERROR_BROKEN_EXIT_HDDGAME: return {"core.CELL_GAME_ERROR_BROKEN_EXIT_HDDGAME", "ERROR: HDD boot game is corrupted. The application will be terminated."};
	case id::CELL_GAME_ERROR_NOSPACE: return {"core.CELL_GAME_ERROR_NOSPACE", "ERROR: Not enough available space. The application will continue.\nSpace needed: %0 KB"};
	case id::CELL_GAME_ERROR_NOSPACE_EXIT: return {"core.CELL_GAME_ERROR_NOSPACE_EXIT", "ERROR: Not enough available space. The application will be terminated.\nSpace needed: %0 KB"};
	case id::CELL_GAME_ERROR_DIR_NAME: return {"core.CELL_GAME_ERROR_DIR_NAME", "Directory name: %0"};
	case id::CELL_GAME_DATA_EXIT_BROKEN: return {"core.CELL_GAME_DATA_EXIT_BROKEN", "There has been an error!\n\nPlease remove the game data for this title."};
	case id::CELL_HDD_GAME_EXIT_BROKEN: return {"core.CELL_HDD_GAME_EXIT_BROKEN", "There has been an error!\n\nPlease reinstall the HDD boot game."};
	case id::CELL_HDD_GAME_CHECK_NOSPACE: return {"core.CELL_HDD_GAME_CHECK_NOSPACE", "Not enough space to create HDD boot game.\nSpace Needed: %0 KB"};
	case id::CELL_HDD_GAME_CHECK_BROKEN: return {"core.CELL_HDD_GAME_CHECK_BROKEN", "HDD boot game %0 is corrupt!"};
	case id::CELL_HDD_GAME_CHECK_NODATA: return {"core.CELL_HDD_GAME_CHECK_NODATA", "HDD boot game %0 could not be found!"};
	case id::CELL_HDD_GAME_CHECK_INVALID: return {"core.CELL_HDD_GAME_CHECK_INVALID", "Error: %0"};
	case id::CELL_GAMEDATA_CHECK_NOSPACE: return {"core.CELL_GAMEDATA_CHECK_NOSPACE", "Not enough space to create game data.\nSpace Needed: %0 KB"};
	case id::CELL_GAMEDATA_CHECK_BROKEN: return {"core.CELL_GAMEDATA_CHECK_BROKEN", "The game data in %0 is corrupt!"};
	case id::CELL_GAMEDATA_CHECK_NODATA: return {"core.CELL_GAMEDATA_CHECK_NODATA", "The game data in %0 could not be found!"};
	case id::CELL_GAMEDATA_CHECK_INVALID: return {"core.CELL_GAMEDATA_CHECK_INVALID", "Error: %0"};

	case id::CELL_MSG_DIALOG_ERROR_DEFAULT: return {"core.CELL_MSG_DIALOG_ERROR_DEFAULT", "An error has occurred.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010001: return {"core.CELL_MSG_DIALOG_ERROR_80010001", "The resource is temporarily unavailable.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010002: return {"core.CELL_MSG_DIALOG_ERROR_80010002", "Invalid argument or flag.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010003: return {"core.CELL_MSG_DIALOG_ERROR_80010003", "The feature is not yet implemented.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010004: return {"core.CELL_MSG_DIALOG_ERROR_80010004", "Memory allocation failed.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010005: return {"core.CELL_MSG_DIALOG_ERROR_80010005", "The resource with the specified identifier does not exist.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010006: return {"core.CELL_MSG_DIALOG_ERROR_80010006", "The file does not exist.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010007: return {"core.CELL_MSG_DIALOG_ERROR_80010007", "The file is in an unrecognized format / The file is not a valid ELF file.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010008: return {"core.CELL_MSG_DIALOG_ERROR_80010008", "Resource deadlock is avoided.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010009: return {"core.CELL_MSG_DIALOG_ERROR_80010009", "Operation not permitted.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_8001000A: return {"core.CELL_MSG_DIALOG_ERROR_8001000A", "The device or resource is busy.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_8001000B: return {"core.CELL_MSG_DIALOG_ERROR_8001000B", "The operation is timed out.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_8001000C: return {"core.CELL_MSG_DIALOG_ERROR_8001000C", "The operation is aborted.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_8001000D: return {"core.CELL_MSG_DIALOG_ERROR_8001000D", "Invalid memory access.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_8001000F: return {"core.CELL_MSG_DIALOG_ERROR_8001000F", "State of the target thread is invalid.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010010: return {"core.CELL_MSG_DIALOG_ERROR_80010010", "Alignment is invalid.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010011: return {"core.CELL_MSG_DIALOG_ERROR_80010011", "Shortage of the kernel resources.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010012: return {"core.CELL_MSG_DIALOG_ERROR_80010012", "The file is a directory.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010013: return {"core.CELL_MSG_DIALOG_ERROR_80010013", "Operation cancelled.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010014: return {"core.CELL_MSG_DIALOG_ERROR_80010014", "Entry already exists.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010015: return {"core.CELL_MSG_DIALOG_ERROR_80010015", "Port is already connected.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010016: return {"core.CELL_MSG_DIALOG_ERROR_80010016", "Port is not connected.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010017: return {"core.CELL_MSG_DIALOG_ERROR_80010017", "Failure in authorizing SELF. Program authentication fail.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010018: return {"core.CELL_MSG_DIALOG_ERROR_80010018", "The file is not MSELF.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010019: return {"core.CELL_MSG_DIALOG_ERROR_80010019", "System version error.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_8001001A: return {"core.CELL_MSG_DIALOG_ERROR_8001001A", "Fatal system error occurred while authorizing SELF. SELF auth failure.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_8001001B: return {"core.CELL_MSG_DIALOG_ERROR_8001001B", "Math domain violation.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_8001001C: return {"core.CELL_MSG_DIALOG_ERROR_8001001C", "Math range violation.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_8001001D: return {"core.CELL_MSG_DIALOG_ERROR_8001001D", "Illegal multi-byte sequence in input.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_8001001E: return {"core.CELL_MSG_DIALOG_ERROR_8001001E", "File position error.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_8001001F: return {"core.CELL_MSG_DIALOG_ERROR_8001001F", "Syscall was interrupted.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010020: return {"core.CELL_MSG_DIALOG_ERROR_80010020", "File too large.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010021: return {"core.CELL_MSG_DIALOG_ERROR_80010021", "Too many links.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010022: return {"core.CELL_MSG_DIALOG_ERROR_80010022", "File table overflow.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010023: return {"core.CELL_MSG_DIALOG_ERROR_80010023", "No space left on device.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010024: return {"core.CELL_MSG_DIALOG_ERROR_80010024", "Not a TTY.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010025: return {"core.CELL_MSG_DIALOG_ERROR_80010025", "Broken pipe.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010026: return {"core.CELL_MSG_DIALOG_ERROR_80010026", "Read-only filesystem.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010027: return {"core.CELL_MSG_DIALOG_ERROR_80010027", "Illegal seek.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010028: return {"core.CELL_MSG_DIALOG_ERROR_80010028", "Arg list too long.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010029: return {"core.CELL_MSG_DIALOG_ERROR_80010029", "Access violation.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_8001002A: return {"core.CELL_MSG_DIALOG_ERROR_8001002A", "Invalid file descriptor.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_8001002B: return {"core.CELL_MSG_DIALOG_ERROR_8001002B", "Filesystem mounting failed.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_8001002C: return {"core.CELL_MSG_DIALOG_ERROR_8001002C", "Too many files open.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_8001002D: return {"core.CELL_MSG_DIALOG_ERROR_8001002D", "No device.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_8001002E: return {"core.CELL_MSG_DIALOG_ERROR_8001002E", "Not a directory.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_8001002F: return {"core.CELL_MSG_DIALOG_ERROR_8001002F", "No such device or IO.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010030: return {"core.CELL_MSG_DIALOG_ERROR_80010030", "Cross-device link error.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010031: return {"core.CELL_MSG_DIALOG_ERROR_80010031", "Bad Message.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010032: return {"core.CELL_MSG_DIALOG_ERROR_80010032", "In progress.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010033: return {"core.CELL_MSG_DIALOG_ERROR_80010033", "Message size error.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010034: return {"core.CELL_MSG_DIALOG_ERROR_80010034", "Name too long.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010035: return {"core.CELL_MSG_DIALOG_ERROR_80010035", "No lock.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010036: return {"core.CELL_MSG_DIALOG_ERROR_80010036", "Not empty.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010037: return {"core.CELL_MSG_DIALOG_ERROR_80010037", "Not supported.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010038: return {"core.CELL_MSG_DIALOG_ERROR_80010038", "File-system specific error.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_80010039: return {"core.CELL_MSG_DIALOG_ERROR_80010039", "Overflow occurred.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_8001003A: return {"core.CELL_MSG_DIALOG_ERROR_8001003A", "Filesystem not mounted.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_8001003B: return {"core.CELL_MSG_DIALOG_ERROR_8001003B", "Not SData.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_8001003C: return {"core.CELL_MSG_DIALOG_ERROR_8001003C", "Incorrect version in sys_load_param.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_8001003D: return {"core.CELL_MSG_DIALOG_ERROR_8001003D", "Pointer is null.\n(%0)"};
	case id::CELL_MSG_DIALOG_ERROR_8001003E: return {"core.CELL_MSG_DIALOG_ERROR_8001003E", "Pointer is null.\n(%0)"};

	case id::CELL_OSK_DIALOG_TITLE: return {"core.CELL_OSK_DIALOG_TITLE", "On Screen Keyboard"};
	case id::CELL_OSK_DIALOG_BUSY: return {"core.CELL_OSK_DIALOG_BUSY", "The Home Menu can't be opened while the On Screen Keyboard is busy!"};
	case id::CELL_SAVEDATA_CB_BROKEN: return {"core.CELL_SAVEDATA_CB_BROKEN", "Error - Save data corrupted"};
	case id::CELL_SAVEDATA_CB_FAILURE: return {"core.CELL_SAVEDATA_CB_FAILURE", "Error - Failed to save or load"};
	case id::CELL_SAVEDATA_CB_NO_DATA: return {"core.CELL_SAVEDATA_CB_NO_DATA", "Error - Save data cannot be found"};
	case id::CELL_SAVEDATA_CB_NO_SPACE: return {"core.CELL_SAVEDATA_CB_NO_SPACE", "Error - Insufficient free space\n\nSpace needed: %0 KB"};
	case id::CELL_SAVEDATA_NO_DATA: return {"core.CELL_SAVEDATA_NO_DATA", "There is no saved data."};
	case id::CELL_SAVEDATA_NEW_SAVED_DATA_TITLE: return {"core.CELL_SAVEDATA_NEW_SAVED_DATA_TITLE", "New Saved Data"};
	case id::CELL_SAVEDATA_NEW_SAVED_DATA_SUB_TITLE: return {"core.CELL_SAVEDATA_NEW_SAVED_DATA_SUB_TITLE", "Select to create a new entry"};
	case id::CELL_SAVEDATA_SAVE_CONFIRMATION: return {"core.CELL_SAVEDATA_SAVE_CONFIRMATION", "Do you want to save this data?"};
	case id::CELL_SAVEDATA_DELETE_CONFIRMATION: return {"core.CELL_SAVEDATA_DELETE_CONFIRMATION", "Do you really want to delete this data?\n\n%0"};
	case id::CELL_SAVEDATA_DELETE_SUCCESS: return {"core.CELL_SAVEDATA_DELETE_SUCCESS", "Successfully removed data!\n\n%0"};
	case id::CELL_SAVEDATA_DELETE: return {"core.CELL_SAVEDATA_DELETE", "Delete this data?\n\n%0"};
	case id::CELL_SAVEDATA_SAVE: return {"core.CELL_SAVEDATA_SAVE", "Save this data?\n\n%0"};
	case id::CELL_SAVEDATA_LOAD: return {"core.CELL_SAVEDATA_LOAD", "Load this data?\n\n%0"};
	case id::CELL_SAVEDATA_OVERWRITE: return {"core.CELL_SAVEDATA_OVERWRITE", "Do you want to overwrite the saved data?\n\n%0"};
	case id::CELL_SAVEDATA_AUTOSAVE: return {"core.CELL_SAVEDATA_AUTOSAVE", "Saving..."};
	case id::CELL_SAVEDATA_AUTOLOAD: return {"core.CELL_SAVEDATA_AUTOLOAD", "Loading..."};

	case id::CELL_CROSS_CONTROLLER_MSG: return {"core.CELL_CROSS_CONTROLLER_MSG", "Start [%0] on the PS Vita system.\nIf you have not installed [%0], go to [Remote Play] on the PS Vita system and start [Cross-Controller] from the LiveArea™ screen."};
	case id::CELL_CROSS_CONTROLLER_FW_MSG: return {"core.CELL_CROSS_CONTROLLER_FW_MSG", "If your system software version on the PS Vita system is earlier than 1.80, you must update the system software to the latest version."};

	case id::CELL_NP_RECVMESSAGE_DIALOG_TITLE: return {"core.CELL_NP_RECVMESSAGE_DIALOG_TITLE", "Select Message"};
	case id::CELL_NP_RECVMESSAGE_DIALOG_TITLE_INVITE: return {"core.CELL_NP_RECVMESSAGE_DIALOG_TITLE_INVITE", "Select Invite"};
	case id::CELL_NP_RECVMESSAGE_DIALOG_TITLE_ADD_FRIEND: return {"core.CELL_NP_RECVMESSAGE_DIALOG_TITLE_ADD_FRIEND", "Add Friend"};
	case id::CELL_NP_RECVMESSAGE_DIALOG_FROM: return {"core.CELL_NP_RECVMESSAGE_DIALOG_FROM", "From:"};
	case id::CELL_NP_RECVMESSAGE_DIALOG_SUBJECT: return {"core.CELL_NP_RECVMESSAGE_DIALOG_SUBJECT", "Subject:"};
	case id::CELL_NP_SENDMESSAGE_DIALOG_TITLE: return {"core.CELL_NP_SENDMESSAGE_DIALOG_TITLE", "Select Message To Send"};
	case id::CELL_NP_SENDMESSAGE_DIALOG_TITLE_INVITE: return {"core.CELL_NP_SENDMESSAGE_DIALOG_TITLE_INVITE", "Send Invite"};
	case id::CELL_NP_SENDMESSAGE_DIALOG_TITLE_ADD_FRIEND: return {"core.CELL_NP_SENDMESSAGE_DIALOG_TITLE_ADD_FRIEND", "Add Friend"};
	case id::CELL_NP_SENDMESSAGE_DIALOG_CONFIRMATION: return {"core.CELL_NP_SENDMESSAGE_DIALOG_CONFIRMATION", "Send message to %0 ?\n\nSubject:"};
	case id::CELL_NP_SENDMESSAGE_DIALOG_CONFIRMATION_INVITE: return {"core.CELL_NP_SENDMESSAGE_DIALOG_CONFIRMATION_INVITE", "Send invite to %0 ?\n\nSubject:"};
	case id::CELL_NP_SENDMESSAGE_DIALOG_CONFIRMATION_ADD_FRIEND: return {"core.CELL_NP_SENDMESSAGE_DIALOG_CONFIRMATION_ADD_FRIEND", "Send friend request to %0 ?\n\nSubject:"};
	case id::CELL_NP_MESSAGE_INVITE_RECEIVED: return {"core.CELL_NP_MESSAGE_INVITE_RECEIVED", "Received an invite from %0"};
	case id::CELL_NP_MESSAGE_OTHER_RECEIVED: return {"core.CELL_NP_MESSAGE_OTHER_RECEIVED", "Received a message from %0"};

	case id::RECORDING_ABORTED: return {"core.RECORDING_ABORTED", "Recording aborted!"};
	case id::RPCN_NO_ERROR: return {"core.RPCN_NO_ERROR", "RPCN: No Error"};
	case id::RPCN_ERROR_INVALID_INPUT: return {"core.RPCN_ERROR_INVALID_INPUT", "RPCN: Invalid Input (Wrong Host/Port)"};
	case id::RPCN_ERROR_WOLFSSL: return {"core.RPCN_ERROR_WOLFSSL", "RPCN Connection Error: WolfSSL Error"};
	case id::RPCN_ERROR_RESOLVE: return {"core.RPCN_ERROR_RESOLVE", "RPCN Connection Error: Resolve Error"};
	case id::RPCN_ERROR_BINDING: return {"core.RPCN_ERROR_BINDING", "RPCN Connection Error: Failed to bind to given binding IP"};
	case id::RPCN_ERROR_CONNECT: return {"core.RPCN_ERROR_CONNECT", "RPCN Connection Error"};
	case id::RPCN_ERROR_LOGIN_ERROR: return {"core.RPCN_ERROR_LOGIN_ERROR", "RPCN Login Error: Identification Error"};
	case id::RPCN_ERROR_ALREADY_LOGGED: return {"core.RPCN_ERROR_ALREADY_LOGGED", "RPCN Login Error: User Already Logged In"};
	case id::RPCN_ERROR_INVALID_LOGIN: return {"core.RPCN_ERROR_INVALID_LOGIN", "RPCN Login Error: Invalid Username"};
	case id::RPCN_ERROR_INVALID_PASSWORD: return {"core.RPCN_ERROR_INVALID_PASSWORD", "RPCN Login Error: Invalid Password"};
	case id::RPCN_ERROR_INVALID_TOKEN: return {"core.RPCN_ERROR_INVALID_TOKEN", "RPCN Login Error: Invalid Token"};
	case id::RPCN_ERROR_INVALID_PROTOCOL_VERSION: return {"core.RPCN_ERROR_INVALID_PROTOCOL_VERSION", "RPCN Misc Error: Protocol Version Error (outdated RPCS3?)"};
	case id::RPCN_ERROR_UNKNOWN: return {"core.RPCN_ERROR_UNKNOWN", "RPCN: Unknown Error"};
	case id::RPCN_SUCCESS_LOGGED_ON: return {"core.RPCN_SUCCESS_LOGGED_ON", "Successfully logged on RPCN!"};
	case id::RPCN_FRIEND_REQUEST_RECEIVED: return {"core.RPCN_FRIEND_REQUEST_RECEIVED", "RPCN: Received friend request: %0"};
	case id::RPCN_FRIEND_ADDED: return {"core.RPCN_FRIEND_ADDED", "RPCN: Friend added: %0"};
	case id::RPCN_FRIEND_LOST: return {"core.RPCN_FRIEND_LOST", "RPCN: Friend removed: %0"};
	case id::RPCN_FRIEND_LOGGED_IN: return {"core.RPCN_FRIEND_LOGGED_IN", "RPCN: %0 logged in"};
	case id::RPCN_FRIEND_LOGGED_OUT: return {"core.RPCN_FRIEND_LOGGED_OUT", "RPCN: %0 logged out"};

	case id::HOME_MENU_TITLE: return {"core.HOME_MENU_TITLE", "Home Menu"};
	case id::HOME_MENU_EXIT_GAME: return {"core.HOME_MENU_EXIT_GAME", "Exit Game"};
	case id::HOME_MENU_RESTART: return {"core.HOME_MENU_RESTART", "Restart Game"};
	case id::HOME_MENU_RESUME: return {"core.HOME_MENU_RESUME", "Resume Game"};
	case id::HOME_MENU_FRIENDS: return {"core.HOME_MENU_FRIENDS", "Friends"};
	case id::HOME_MENU_FRIENDS_REQUESTS: return {"core.HOME_MENU_FRIENDS_REQUESTS", "Pending Friend Requests"};
	case id::HOME_MENU_FRIENDS_GAME_INVITES: return {"core.HOME_MENU_FRIENDS_GAME_INVITES", "Game Invitations"};
	case id::HOME_MENU_FRIENDS_BLOCKED: return {"core.HOME_MENU_FRIENDS_BLOCKED", "Blocked Users"};
	case id::HOME_MENU_FRIENDS_STATUS_ONLINE: return {"core.HOME_MENU_FRIENDS_STATUS_ONLINE", "Online"};
	case id::HOME_MENU_FRIENDS_STATUS_OFFLINE: return {"core.HOME_MENU_FRIENDS_STATUS_OFFLINE", "Offline"};
	case id::HOME_MENU_FRIENDS_STATUS_BLOCKED: return {"core.HOME_MENU_FRIENDS_STATUS_BLOCKED", "Blocked"};
	case id::HOME_MENU_FRIENDS_REQUEST_SENT: return {"core.HOME_MENU_FRIENDS_REQUEST_SENT", "You sent a friend request"};
	case id::HOME_MENU_FRIENDS_REQUEST_RECEIVED: return {"core.HOME_MENU_FRIENDS_REQUEST_RECEIVED", "Sent you a friend request"};
	case id::HOME_MENU_FRIENDS_BLOCK_USER_MSG: return {"core.HOME_MENU_FRIENDS_BLOCK_USER_MSG", "Block this user?\n\n%0"};
	case id::HOME_MENU_FRIENDS_UNBLOCK_USER_MSG: return {"core.HOME_MENU_FRIENDS_UNBLOCK_USER_MSG", "Unblock this user?\n\n%0"};
	case id::HOME_MENU_FRIENDS_REMOVE_USER_MSG: return {"core.HOME_MENU_FRIENDS_REMOVE_USER_MSG", "Remove this user?\n\n%0"};
	case id::HOME_MENU_FRIENDS_ACCEPT_REQUEST_MSG: return {"core.HOME_MENU_FRIENDS_ACCEPT_REQUEST_MSG", "Accept Request?\n\n%0"};
	case id::HOME_MENU_FRIENDS_CANCEL_REQUEST_MSG: return {"core.HOME_MENU_FRIENDS_CANCEL_REQUEST_MSG", "Cancel Request?\n\n%0"};
	case id::HOME_MENU_FRIENDS_REJECT_REQUEST_MSG: return {"core.HOME_MENU_FRIENDS_REJECT_REQUEST_MSG", "Reject Request?\n\n%0"};
	case id::HOME_MENU_FRIENDS_REJECT_REQUEST: return {"core.HOME_MENU_FRIENDS_REJECT_REQUEST", "Reject Request"};
	case id::HOME_MENU_FRIENDS_ACCEPT_GAME_INVITE_MSG: return {"core.HOME_MENU_FRIENDS_ACCEPT_GAME_INVITE_MSG", "Accept game invitation from %0?"};
	case id::HOME_MENU_FRIENDS_REJECT_GAME_INVITE_MSG: return {"core.HOME_MENU_FRIENDS_REJECT_GAME_INVITE_MSG", "Reject game invitation from %0?"};
	case id::HOME_MENU_FRIENDS_REJECT_GAME_INVITE: return {"core.HOME_MENU_FRIENDS_REJECT_GAME_INVITE", "Reject Invitation"};
	case id::HOME_MENU_FRIENDS_NEXT_LIST: return {"core.HOME_MENU_FRIENDS_NEXT_LIST", "Next list"};
	case id::HOME_MENU_SETTINGS: return {"core.HOME_MENU_SETTINGS", "Settings"};
	case id::HOME_MENU_SETTINGS_SAVE: return {"core.HOME_MENU_SETTINGS_SAVE", "Save custom configuration?"};
	case id::HOME_MENU_SETTINGS_SAVE_BUTTON: return {"core.HOME_MENU_SETTINGS_SAVE_BUTTON", "Save"};
	case id::HOME_MENU_SETTINGS_DISCARD: return {"core.HOME_MENU_SETTINGS_DISCARD", "Discard the current settings' changes?"};
	case id::HOME_MENU_SETTINGS_DISCARD_BUTTON: return {"core.HOME_MENU_SETTINGS_DISCARD_BUTTON", "Discard"};
	case id::HOME_MENU_SETTINGS_RESET_BUTTON: return {"core.HOME_MENU_SETTINGS_RESET_BUTTON", "To default"};
	case id::HOME_MENU_SETTINGS_AUDIO: return {"core.HOME_MENU_SETTINGS_AUDIO", "Audio"};
	case id::HOME_MENU_SETTINGS_AUDIO_MASTER_VOLUME: return {"core.HOME_MENU_SETTINGS_AUDIO_MASTER_VOLUME", "Master Volume"};
	case id::HOME_MENU_SETTINGS_AUDIO_BACKEND: return {"core.HOME_MENU_SETTINGS_AUDIO_BACKEND", "Audio Backend"};
	case id::HOME_MENU_SETTINGS_AUDIO_BUFFERING: return {"core.HOME_MENU_SETTINGS_AUDIO_BUFFERING", "Enable Buffering"};
	case id::HOME_MENU_SETTINGS_AUDIO_BUFFER_DURATION: return {"core.HOME_MENU_SETTINGS_AUDIO_BUFFER_DURATION", "Desired Audio Buffer Duration"};
	case id::HOME_MENU_SETTINGS_AUDIO_TIME_STRETCHING: return {"core.HOME_MENU_SETTINGS_AUDIO_TIME_STRETCHING", "Enable Time Stretching"};
	case id::HOME_MENU_SETTINGS_AUDIO_TIME_STRETCHING_THRESHOLD: return {"core.HOME_MENU_SETTINGS_AUDIO_TIME_STRETCHING_THRESHOLD", "Time Stretching Threshold"};
	case id::HOME_MENU_SETTINGS_VIDEO: return {"core.HOME_MENU_SETTINGS_VIDEO", "Video"};
	case id::HOME_MENU_SETTINGS_VIDEO_VSYNC: return {"core.HOME_MENU_SETTINGS_VIDEO_VSYNC", "VSync"};
	case id::HOME_MENU_SETTINGS_VIDEO_FRAME_LIMIT: return {"core.HOME_MENU_SETTINGS_VIDEO_FRAME_LIMIT", "Frame Limit"};
	case id::HOME_MENU_SETTINGS_VIDEO_ANISOTROPIC_OVERRIDE: return {"core.HOME_MENU_SETTINGS_VIDEO_ANISOTROPIC_OVERRIDE", "Anisotropic Filter Override"};
	case id::HOME_MENU_SETTINGS_VIDEO_OUTPUT_SCALING: return {"core.HOME_MENU_SETTINGS_VIDEO_OUTPUT_SCALING", "Output Scaling"};
	case id::HOME_MENU_SETTINGS_VIDEO_RCAS_SHARPENING: return {"core.HOME_MENU_SETTINGS_VIDEO_RCAS_SHARPENING", "FidelityFX CAS Sharpening Intensity"};
	case id::HOME_MENU_SETTINGS_VIDEO_RESOLUTION_SCALE_PERCENT: return {"core.HOME_MENU_SETTINGS_VIDEO_RESOLUTION_SCALE_PERCENT", "Resolution Scale"};
	case id::HOME_MENU_SETTINGS_VIDEO_RESOLUTION_SCALE_THRESHOLD: return {"core.HOME_MENU_SETTINGS_VIDEO_RESOLUTION_SCALE_THRESHOLD", "Resolution Scale Threshold"};
	case id::HOME_MENU_SETTINGS_VIDEO_STRETCH_TO_DISPLAY: return {"core.HOME_MENU_SETTINGS_VIDEO_STRETCH_TO_DISPLAY", "Stretch To Display Area"};
	case id::HOME_MENU_SETTINGS_VIDEO_STEREO_MODE: return {"core.HOME_MENU_SETTINGS_VIDEO_STEREO_MODE", "Stereo Mode"};
	case id::HOME_MENU_SETTINGS_INPUT: return {"core.HOME_MENU_SETTINGS_INPUT", "Input"};
	case id::HOME_MENU_SETTINGS_INPUT_BACKGROUND_INPUT: return {"core.HOME_MENU_SETTINGS_INPUT_BACKGROUND_INPUT", "Background Input Enabled"};
	case id::HOME_MENU_SETTINGS_INPUT_KEEP_PADS_CONNECTED: return {"core.HOME_MENU_SETTINGS_INPUT_KEEP_PADS_CONNECTED", "Keep Pads Connected"};
	case id::HOME_MENU_SETTINGS_INPUT_SHOW_PS_MOVE_CURSOR: return {"core.HOME_MENU_SETTINGS_INPUT_SHOW_PS_MOVE_CURSOR", "Show PS Move Cursor"};
	case id::HOME_MENU_SETTINGS_INPUT_CAMERA_FLIP: return {"core.HOME_MENU_SETTINGS_INPUT_CAMERA_FLIP", "Camera Flip"};
	case id::HOME_MENU_SETTINGS_INPUT_PAD_MODE: return {"core.HOME_MENU_SETTINGS_INPUT_PAD_MODE", "Pad Handler Mode"};
	case id::HOME_MENU_SETTINGS_INPUT_PAD_SLEEP: return {"core.HOME_MENU_SETTINGS_INPUT_PAD_SLEEP", "Pad Handler Sleep"};
	case id::HOME_MENU_SETTINGS_INPUT_FAKE_MOVE_ROTATION_CONE_H: return {"core.HOME_MENU_SETTINGS_INPUT_FAKE_MOVE_ROTATION_CONE_H", "Fake PS Move Rotation Cone (Horizontal)"};
	case id::HOME_MENU_SETTINGS_INPUT_FAKE_MOVE_ROTATION_CONE_V: return {"core.HOME_MENU_SETTINGS_INPUT_FAKE_MOVE_ROTATION_CONE_V", "Fake PS Move Rotation Cone (Vertical)"};
	case id::HOME_MENU_SETTINGS_ADVANCED: return {"core.HOME_MENU_SETTINGS_ADVANCED", "Advanced"};
	case id::HOME_MENU_SETTINGS_ADVANCED_PREFERRED_SPU_THREADS: return {"core.HOME_MENU_SETTINGS_ADVANCED_PREFERRED_SPU_THREADS", "Preferred SPU Threads"};
	case id::HOME_MENU_SETTINGS_ADVANCED_MAX_CPU_PREEMPTIONS: return {"core.HOME_MENU_SETTINGS_ADVANCED_MAX_CPU_PREEMPTIONS", "Max Power Saving CPU-Preemptions"};
	case id::HOME_MENU_SETTINGS_ADVANCED_ACCURATE_RSX_RESERVATION_ACCESS: return {"core.HOME_MENU_SETTINGS_ADVANCED_ACCURATE_RSX_RESERVATION_ACCESS", "Accurate RSX reservation access"};
	case id::HOME_MENU_SETTINGS_ADVANCED_SLEEP_TIMERS_ACCURACY: return {"core.HOME_MENU_SETTINGS_ADVANCED_SLEEP_TIMERS_ACCURACY", "Sleep Timers Accuracy"};
	case id::HOME_MENU_SETTINGS_ADVANCED_RSX_MEMORY_TILING: return {"core.HOME_MENU_SETTINGS_ADVANCED_RSX_MEMORY_TILING", "Handle RSX Memory Tiling"};
	case id::HOME_MENU_SETTINGS_ADVANCED_MAX_SPURS_THREADS: return {"core.HOME_MENU_SETTINGS_ADVANCED_MAX_SPURS_THREADS", "Max SPURS Threads"};
	case id::HOME_MENU_SETTINGS_ADVANCED_DRIVER_WAKE_UP_DELAY: return {"core.HOME_MENU_SETTINGS_ADVANCED_DRIVER_WAKE_UP_DELAY", "Driver Wake-Up Delay"};
	case id::HOME_MENU_SETTINGS_ADVANCED_VBLANK_FREQUENCY: return {"core.HOME_MENU_SETTINGS_ADVANCED_VBLANK_FREQUENCY", "VBlank Frequency"};
	case id::HOME_MENU_SETTINGS_ADVANCED_VBLANK_NTSC: return {"core.HOME_MENU_SETTINGS_ADVANCED_VBLANK_NTSC", "VBlank NTSC Fixup"};
	case id::HOME_MENU_SETTINGS_OVERLAYS: return {"core.HOME_MENU_SETTINGS_OVERLAYS", "Overlays"};
	case id::HOME_MENU_SETTINGS_OVERLAYS_SHOW_TROPHY_POPUPS: return {"core.HOME_MENU_SETTINGS_OVERLAYS_SHOW_TROPHY_POPUPS", "Show Trophy Popups"};
	case id::HOME_MENU_SETTINGS_OVERLAYS_SHOW_RPCN_POPUPS: return {"core.HOME_MENU_SETTINGS_OVERLAYS_SHOW_RPCN_POPUPS", "Show RPCN Popups"};
	case id::HOME_MENU_SETTINGS_OVERLAYS_SHOW_SHADER_COMPILATION_HINT: return {"core.HOME_MENU_SETTINGS_OVERLAYS_SHOW_SHADER_COMPILATION_HINT", "Show Shader Compilation Hint"};
	case id::HOME_MENU_SETTINGS_OVERLAYS_SHOW_PPU_COMPILATION_HINT: return {"core.HOME_MENU_SETTINGS_OVERLAYS_SHOW_PPU_COMPILATION_HINT", "Show PPU Compilation Hint"};
	case id::HOME_MENU_SETTINGS_OVERLAYS_SHOW_AUTO_SAVE_LOAD_HINT: return {"core.HOME_MENU_SETTINGS_OVERLAYS_SHOW_AUTO_SAVE_LOAD_HINT", "Show Autosave/Autoload Hint"};
	case id::HOME_MENU_SETTINGS_OVERLAYS_SHOW_PRESSURE_INTENSITY_TOGGLE_HINT: return {"core.HOME_MENU_SETTINGS_OVERLAYS_SHOW_PRESSURE_INTENSITY_TOGGLE_HINT", "Show Pressure Intensity Toggle Hint"};
	case id::HOME_MENU_SETTINGS_OVERLAYS_SHOW_ANALOG_LIMITER_TOGGLE_HINT: return {"core.HOME_MENU_SETTINGS_OVERLAYS_SHOW_ANALOG_LIMITER_TOGGLE_HINT", "Show Analog Limiter Toggle Hint"};
	case id::HOME_MENU_SETTINGS_OVERLAYS_SHOW_MOUSE_AND_KB_TOGGLE_HINT: return {"core.HOME_MENU_SETTINGS_OVERLAYS_SHOW_MOUSE_AND_KB_TOGGLE_HINT", "Show Mouse And Keyboard Toggle Hint"};
	case id::HOME_MENU_SETTINGS_OVERLAYS_SHOW_FATAL_ERROR_HINTS: return {"core.HOME_MENU_SETTINGS_OVERLAYS_SHOW_FATAL_ERROR_HINTS", "Show Fatal Error Hints"};
	case id::HOME_MENU_SETTINGS_OVERLAYS_RECORD_WITH_OVERLAYS: return {"core.HOME_MENU_SETTINGS_OVERLAYS_RECORD_WITH_OVERLAYS", "Record With Overlays"};
	case id::HOME_MENU_SETTINGS_OVERLAYS_PLAY_MUSIC_DURING_BOOT: return {"core.HOME_MENU_SETTINGS_OVERLAYS_PLAY_MUSIC_DURING_BOOT", "Play music during boot sequence."};
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY: return {"core.HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY", "Performance Overlay"};
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_ENABLE: return {"core.HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_ENABLE", "Enable Performance Overlay"};
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_ENABLE_FRAMERATE_GRAPH: return {"core.HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_ENABLE_FRAMERATE_GRAPH", "Enable Framerate Graph"};
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_ENABLE_FRAMETIME_GRAPH: return {"core.HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_ENABLE_FRAMETIME_GRAPH", "Enable Frametime Graph"};
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_DETAIL_LEVEL: return {"core.HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_DETAIL_LEVEL", "Detail level"};
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_FRAMERATE_DETAIL_LEVEL: return {"core.HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_FRAMERATE_DETAIL_LEVEL", "Framerate Graph Detail Level"};
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_FRAMETIME_DETAIL_LEVEL: return {"core.HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_FRAMETIME_DETAIL_LEVEL", "Frametime Graph Detail Level"};
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_FRAMERATE_DATAPOINT_COUNT: return {"core.HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_FRAMERATE_DATAPOINT_COUNT", "Framerate Datapoints"};
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_FRAMETIME_DATAPOINT_COUNT: return {"core.HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_FRAMETIME_DATAPOINT_COUNT", "Frametime Datapoints"};
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_UPDATE_INTERVAL: return {"core.HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_UPDATE_INTERVAL", "Metrics Update Interval"};
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_POSITION: return {"core.HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_POSITION", "Position"};
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_CENTER_X: return {"core.HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_CENTER_X", "Center Horizontally"};
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_CENTER_Y: return {"core.HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_CENTER_Y", "Center Vertically"};
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_MARGIN_X: return {"core.HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_MARGIN_X", "Horizontal Margin"};
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_MARGIN_Y: return {"core.HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_MARGIN_Y", "Vertical Margin"};
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_FONT_SIZE: return {"core.HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_FONT_SIZE", "Font Size"};
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_OPACITY: return {"core.HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_OPACITY", "Opacity"};
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_USE_WINDOW_SPACE: return {"core.HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_USE_WINDOW_SPACE", "Use Window Space"};
	case id::HOME_MENU_SETTINGS_DEBUG: return {"core.HOME_MENU_SETTINGS_DEBUG", "Debug"};
	case id::HOME_MENU_SETTINGS_DEBUG_OVERLAY: return {"core.HOME_MENU_SETTINGS_DEBUG_OVERLAY", "Debug Overlay"};
	case id::HOME_MENU_SETTINGS_DEBUG_INPUT_OVERLAY: return {"core.HOME_MENU_SETTINGS_DEBUG_INPUT_OVERLAY", "Input Debug Overlay"};
	case id::HOME_MENU_SETTINGS_MOUSE_DEBUG_INPUT_OVERLAY: return {"core.HOME_MENU_SETTINGS_MOUSE_DEBUG_INPUT_OVERLAY", "Mouse Debug Overlay"};
	case id::HOME_MENU_SETTINGS_DEBUG_DISABLE_VIDEO_OUTPUT: return {"core.HOME_MENU_SETTINGS_DEBUG_DISABLE_VIDEO_OUTPUT", "Disable Video Output"};
	case id::HOME_MENU_SETTINGS_DEBUG_TEXTURE_LOD_BIAS: return {"core.HOME_MENU_SETTINGS_DEBUG_TEXTURE_LOD_BIAS", "Texture LOD Bias Addend"};
	case id::HOME_MENU_SETTINGS_SYSTEM_START_BIG_PICTURE_MODE: return {"core.HOME_MENU_SETTINGS_SYSTEM_START_BIG_PICTURE_MODE", "Open Big Picture Mode On Boot"};
	case id::HOME_MENU_SCREENSHOT: return {"core.HOME_MENU_SCREENSHOT", "Take Screenshot"};
	case id::HOME_MENU_SAVESTATE: return {"core.HOME_MENU_SAVESTATE", "SaveState"};
	case id::HOME_MENU_SAVESTATE_SAVE: return {"core.HOME_MENU_SAVESTATE_SAVE", "Save Emulation State"};
	case id::HOME_MENU_SAVESTATE_AND_EXIT: return {"core.HOME_MENU_SAVESTATE_AND_EXIT", "Save Emulation State And Exit"};
	case id::HOME_MENU_RELOAD_SAVESTATE: return {"core.HOME_MENU_RELOAD_SAVESTATE", "Reload Last Emulation State"};
	case id::HOME_MENU_RELOAD_SECOND_SAVESTATE: return {"core.HOME_MENU_RELOAD_SECOND_SAVESTATE", "Reload Second-To-Last Emulation State"};
	case id::HOME_MENU_RELOAD_THIRD_SAVESTATE: return {"core.HOME_MENU_RELOAD_THIRD_SAVESTATE", "Reload Third-To-Last Emulation State"};
	case id::HOME_MENU_RELOAD_FOURTH_SAVESTATE: return {"core.HOME_MENU_RELOAD_FOURTH_SAVESTATE", "Reload Fourth-To-Last Emulation State"};
	case id::SAVESTATE_FAILED_DUE_TO_VDEC: return {"core.SAVESTATE_FAILED_DUE_TO_VDEC", "SaveState failed: a video or cutscene is active. Wait for it to finish and try again."};
	case id::SAVESTATE_FAILED_DUE_TO_SAVEDATA: return {"core.SAVESTATE_FAILED_DUE_TO_SAVEDATA", "SaveState failed: the game is saving data. Wait for it to finish and try again."};
	case id::SAVESTATE_FAILED_DUE_TO_SPU: return {"core.SAVESTATE_FAILED_DUE_TO_SPU", "SaveState failed: RPCS3 could not safely lock the SPU state."};
	case id::SAVESTATE_FAILED_DUE_TO_MISSING_SPU_SETTING: return {"core.SAVESTATE_FAILED_DUE_TO_MISSING_SPU_SETTING", "SaveState failed: enable Advanced > Compatible Savestate Mode, restart the game, and try again. This can reduce performance."};
	case id::BIG_PICTURE_MODE_TITLE: return {"core.BIG_PICTURE_MODE_TITLE", "Big Picture Mode"};
	case id::BIG_PICTURE_MENU_GAMES: return {"core.BIG_PICTURE_MENU_GAMES", "Games"};
	case id::BIG_PICTURE_MENU_EXIT: return {"core.BIG_PICTURE_MENU_EXIT", "Exit Big Picture Mode"};
	case id::BIG_PICTURE_NO_GAMES_FOUND: return {"core.BIG_PICTURE_NO_GAMES_FOUND", "No games found.\nAdd games in the main RPCS3 window."};
	case id::BIG_PICTURE_GAME_DETAILS_START: return {"core.BIG_PICTURE_GAME_DETAILS_START", "Start"};
	case id::BIG_PICTURE_HINT_BACK: return {"core.BIG_PICTURE_HINT_BACK", "Back"};
	case id::BIG_PICTURE_HINT_SELECT: return {"core.BIG_PICTURE_HINT_SELECT", "Select"};
	case id::HOME_MENU_TOGGLE_FULLSCREEN: return {"core.HOME_MENU_TOGGLE_FULLSCREEN", "Toggle Fullscreen"};
	case id::HOME_MENU_RECORDING: return {"core.HOME_MENU_RECORDING", "Start/Stop Recording"};
	case id::HOME_MENU_TROPHIES: return {"core.HOME_MENU_TROPHIES", "Trophies"};
	case id::HOME_MENU_TROPHY_LIST_TITLE: return {"core.HOME_MENU_TROPHY_LIST_TITLE", "Trophy Progress: %0"};
	case id::HOME_MENU_TROPHY_LOCKED_TITLE: return {"core.HOME_MENU_TROPHY_LOCKED_TITLE", "Locked trophy: %0"};
	case id::HOME_MENU_TROPHY_HIDDEN_TITLE: return {"core.HOME_MENU_TROPHY_HIDDEN_TITLE", "Hidden trophy"};
	case id::HOME_MENU_TROPHY_HIDDEN_DESCRIPTION: return {"core.HOME_MENU_TROPHY_HIDDEN_DESCRIPTION", "This trophy is hidden"};
	case id::HOME_MENU_TROPHY_SHOW_HIDDEN_TROPHIES: return {"core.HOME_MENU_TROPHY_SHOW_HIDDEN_TROPHIES", "Show hidden trophies"};
	case id::HOME_MENU_TROPHY_HIDE_HIDDEN_TROPHIES: return {"core.HOME_MENU_TROPHY_HIDE_HIDDEN_TROPHIES", "Hide hidden trophies"};
	case id::HOME_MENU_TROPHY_PLATINUM_RELEVANT: return {"core.HOME_MENU_TROPHY_PLATINUM_RELEVANT", "Platinum relevant"};
	case id::HOME_MENU_TROPHY_GRADE_BRONZE: return {"core.HOME_MENU_TROPHY_GRADE_BRONZE", "Bronze"};
	case id::HOME_MENU_TROPHY_GRADE_SILVER: return {"core.HOME_MENU_TROPHY_GRADE_SILVER", "Silver"};
	case id::HOME_MENU_TROPHY_GRADE_GOLD: return {"core.HOME_MENU_TROPHY_GRADE_GOLD", "Gold"};
	case id::HOME_MENU_TROPHY_GRADE_PLATINUM: return {"core.HOME_MENU_TROPHY_GRADE_PLATINUM", "Platinum"};
	case id::HOME_MENU_TROPHY_SYNC_TROPHIES: return {"core.HOME_MENU_TROPHY_SYNC_TROPHIES", "Sync trophies"};
	case id::HOME_MENU_TROPHY_SYNCING_TROPHIES: return {"core.HOME_MENU_TROPHY_SYNCING_TROPHIES", "Syncing..."};
	case id::HOME_MENU_TROPHY_SYNC_SUCCESS: return {"core.HOME_MENU_TROPHY_SYNC_SUCCESS", "Synced!"};
	case id::HOME_MENU_TROPHY_SYNC_FAILED: return {"core.HOME_MENU_TROPHY_SYNC_FAILED", "Sync failed"};
	case id::HOME_MENU_TROPHY_SORT_GAME_DEFAULT: return {"core.HOME_MENU_TROPHY_SORT_GAME_DEFAULT", "Sort: Game Default"};
	case id::HOME_MENU_TROPHY_SORT_NOT_EARNED: return {"core.HOME_MENU_TROPHY_SORT_NOT_EARNED", "Sort: Not Earned"};
	case id::HOME_MENU_TROPHY_SORT_EARNED_DATE: return {"core.HOME_MENU_TROPHY_SORT_EARNED_DATE", "Sort: Earned Date"};
	case id::HOME_MENU_TROPHY_SORT_GRADE: return {"core.HOME_MENU_TROPHY_SORT_GRADE", "Sort: Grade"};

	case id::AUDIO_MUTED: return {"core.AUDIO_MUTED", "Audio muted"};
	case id::AUDIO_UNMUTED: return {"core.AUDIO_UNMUTED", "Audio unmuted"};
	case id::AUDIO_CHANGED: return {"core.AUDIO_CHANGED", "Volume changed to %0"};
	case id::PROGRESS_DIALOG_PROGRESS: return {"core.PROGRESS_DIALOG_PROGRESS", "Progress:"};
	case id::PROGRESS_DIALOG_PROGRESS_ANALYZING: return {"core.PROGRESS_DIALOG_PROGRESS_ANALYZING", "Progress: analyzing..."};
	case id::PROGRESS_DIALOG_REMAINING: return {"core.PROGRESS_DIALOG_REMAINING", "remaining"};
	case id::PROGRESS_DIALOG_DONE: return {"core.PROGRESS_DIALOG_DONE", "done"};
	case id::PROGRESS_DIALOG_FILE: return {"core.PROGRESS_DIALOG_FILE", "file"};
	case id::PROGRESS_DIALOG_MODULE: return {"core.PROGRESS_DIALOG_MODULE", "module"};
	case id::PROGRESS_DIALOG_OF: return {"core.PROGRESS_DIALOG_OF", "of"};
	case id::PROGRESS_DIALOG_PLEASE_WAIT: return {"core.PROGRESS_DIALOG_PLEASE_WAIT", "Please wait"};
	case id::PROGRESS_DIALOG_STOPPING_PLEASE_WAIT: return {"core.PROGRESS_DIALOG_STOPPING_PLEASE_WAIT", "Stopping. Please wait..."};
	case id::PROGRESS_DIALOG_SAVESTATE_PLEASE_WAIT: return {"core.PROGRESS_DIALOG_SAVESTATE_PLEASE_WAIT", "Creating savestate. Please wait..."};
	case id::PROGRESS_DIALOG_SCANNING_PPU_EXECUTABLE: return {"core.PROGRESS_DIALOG_SCANNING_PPU_EXECUTABLE", "Scanning PPU Executable..."};
	case id::PROGRESS_DIALOG_ANALYZING_PPU_EXECUTABLE: return {"core.PROGRESS_DIALOG_ANALYZING_PPU_EXECUTABLE", "Analyzing PPU Executable..."};
	case id::PROGRESS_DIALOG_SCANNING_PPU_MODULES: return {"core.PROGRESS_DIALOG_SCANNING_PPU_MODULES", "Scanning PPU Modules..."};
	case id::PROGRESS_DIALOG_LOADING_PPU_MODULES: return {"core.PROGRESS_DIALOG_LOADING_PPU_MODULES", "Loading PPU Modules..."};
	case id::PROGRESS_DIALOG_COMPILING_PPU_MODULES: return {"core.PROGRESS_DIALOG_COMPILING_PPU_MODULES", "Compiling PPU Modules..."};
	case id::PROGRESS_DIALOG_LINKING_PPU_MODULES: return {"core.PROGRESS_DIALOG_LINKING_PPU_MODULES", "Linking PPU Modules..."};
	case id::PROGRESS_DIALOG_APPLYING_PPU_CODE: return {"core.PROGRESS_DIALOG_APPLYING_PPU_CODE", "Applying PPU Code..."};
	case id::PROGRESS_DIALOG_BUILDING_SPU_CACHE: return {"core.PROGRESS_DIALOG_BUILDING_SPU_CACHE", "Building SPU Cache..."};
	case id::EMULATION_PAUSED_RESUME_WITH_START: return {"core.EMULATION_PAUSED_RESUME_WITH_START", "Press and hold the START button to resume"};
	case id::EMULATION_RESUMING: return {"core.EMULATION_RESUMING", "Resuming...!"};
	case id::EMULATION_FROZEN: return {"core.EMULATION_FROZEN", "The PS3 application has likely crashed, you can close it."};
	default: return {};
	}
}

std::string resolve_localized_string(
	std::string_view key,
	std::string_view source,
	std::string_view language_tag)
{
	if (source.empty())
	{
		return {};
	}
	if (const localization_resolver resolver = g_localization_resolver.load(std::memory_order_acquire))
	{
		if (std::string localized = resolver(language_tag, key, source); !localized.empty())
		{
			return localized;
		}
	}
	return std::string{source};
}

std::string substitute_argument(std::string_view text, const char* argument)
{
	std::string result{text};
	const std::string_view replacement = argument ? std::string_view{argument} : std::string_view{};
	for (std::size_t position = result.find("%0"); position != std::string::npos; position = result.find("%0", position + replacement.size()))
	{
		result.replace(position, 2, replacement);
	}
	return result;
}

std::u32string utf8_to_u32(std::string_view text)
{
	std::u32string result;
	result.reserve(text.size());
	for (std::size_t index = 0; index < text.size();)
	{
		const auto first = static_cast<unsigned char>(text[index++]);
		char32_t code = 0xfffd;
		std::size_t continuation_count = 0;
		if (first < 0x80)
		{
			code = first;
		}
		else if ((first & 0xe0) == 0xc0)
		{
			code = first & 0x1f;
			continuation_count = 1;
		}
		else if ((first & 0xf0) == 0xe0)
		{
			code = first & 0x0f;
			continuation_count = 2;
		}
		else if ((first & 0xf8) == 0xf0)
		{
			code = first & 0x07;
			continuation_count = 3;
		}

		if (continuation_count > text.size() - index)
		{
			result.push_back(0xfffd);
			break;
		}

		bool valid = true;
		for (std::size_t offset = 0; offset < continuation_count; offset++)
		{
			const auto continuation = static_cast<unsigned char>(text[index + offset]);
			if ((continuation & 0xc0) != 0x80)
			{
				valid = false;
				break;
			}
			code = (code << 6) | (continuation & 0x3f);
		}
		if (valid)
		{
			index += continuation_count;
			result.push_back(code);
		}
		else
		{
			result.push_back(0xfffd);
		}
	}
	return result;
}
}

void set_localization_resolver(localization_resolver resolver) noexcept
{
	g_localization_resolver.store(resolver, std::memory_order_release);
}

std::string localized_overlay_string(id value, std::string_view language_tag, const char* argument)
{
	const localized_source source = overlay_source(value);
	if (!source.english.empty())
	{
		return substitute_argument(resolve_localized_string(source.key, source.english, language_tag), argument);
	}
	return argument ? std::string{argument} : std::string{};
}

std::u32string localized_overlay_u32string(id value, std::string_view language_tag, const char* argument)
{
	return utf8_to_u32(localized_overlay_string(value, language_tag, argument));
}

std::string localized_setting_string(
	std::string_view setting_name,
	std::uint32_t enum_index,
	std::string_view value,
	std::string_view language_tag)
{
	return resolve_localized_string(
		setting_localization_key(setting_name, enum_index, value), value, language_tag);
}
}
