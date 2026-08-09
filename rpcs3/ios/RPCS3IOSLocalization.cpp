#include "RPCS3IOSLocalization.h"

#include "Emu/localized_string_id.h"

namespace rpcs3::ios
{
namespace
{
using id = localized_string_id;

std::string_view english_overlay_string(id value) noexcept
{
	switch (value)
	{
	case id::RSX_OVERLAYS_SPINNER_NO_TEXT: return {};
	case id::RSX_OVERLAYS_TROPHY_BRONZE: return "You have earned a bronze trophy.\n%0";
	case id::RSX_OVERLAYS_TROPHY_SILVER: return "You have earned a silver trophy.\n%0";
	case id::RSX_OVERLAYS_TROPHY_GOLD: return "You have earned a gold trophy.\n%0";
	case id::RSX_OVERLAYS_TROPHY_PLATINUM: return "You have earned a platinum trophy.\n%0";
	case id::RSX_OVERLAYS_COMPILING_SHADERS: return "Compiling shaders";
	case id::RSX_OVERLAYS_COMPILING_PPU_MODULES: return "Compiling PPU Modules";
	case id::RSX_OVERLAYS_MSG_DIALOG_YES: return "Yes";
	case id::RSX_OVERLAYS_MSG_DIALOG_NO: return "No";
	case id::RSX_OVERLAYS_MSG_DIALOG_CANCEL: return "Back";
	case id::RSX_OVERLAYS_MSG_DIALOG_OK: return "OK";
	case id::RSX_OVERLAYS_SAVE_DIALOG_TITLE: return "Save Dialog";
	case id::RSX_OVERLAYS_SAVE_DIALOG_DELETE: return "Delete Save";
	case id::RSX_OVERLAYS_SAVE_DIALOG_LOAD: return "Load Save";
	case id::RSX_OVERLAYS_SAVE_DIALOG_SAVE: return "Save";
	case id::RSX_OVERLAYS_OSK_DIALOG_ACCEPT: return "Enter";
	case id::RSX_OVERLAYS_OSK_DIALOG_CANCEL: return "Back";
	case id::RSX_OVERLAYS_OSK_DIALOG_SPACE: return "Space";
	case id::RSX_OVERLAYS_OSK_DIALOG_BACKSPACE: return "Backspace";
	case id::RSX_OVERLAYS_OSK_DIALOG_SHIFT: return "Shift";
	case id::RSX_OVERLAYS_OSK_DIALOG_ENTER_TEXT: return "[Enter Text]";
	case id::RSX_OVERLAYS_OSK_DIALOG_ENTER_PASSWORD: return "[Enter Password]";
	case id::RSX_OVERLAYS_MEDIA_DIALOG_TITLE: return "Select media";
	case id::RSX_OVERLAYS_MEDIA_DIALOG_TITLE_PHOTO_IMPORT: return "Select photo to import";
	case id::RSX_OVERLAYS_MEDIA_DIALOG_EMPTY: return "No media found.";
	case id::RSX_OVERLAYS_LIST_SELECT: return "Enter";
	case id::RSX_OVERLAYS_LIST_CANCEL: return "Back";
	case id::RSX_OVERLAYS_LIST_DENY: return "Deny";
	case id::RSX_OVERLAYS_PRESSURE_INTENSITY_TOGGLED_OFF: return "Pressure intensity mode of player %0 disabled";
	case id::RSX_OVERLAYS_PRESSURE_INTENSITY_TOGGLED_ON: return "Pressure intensity mode of player %0 enabled";
	case id::RSX_OVERLAYS_ANALOG_LIMITER_TOGGLED_OFF: return "Analog limiter of player %0 disabled";
	case id::RSX_OVERLAYS_ANALOG_LIMITER_TOGGLED_ON: return "Analog limiter of player %0 enabled";
	case id::RSX_OVERLAYS_MOUSE_AND_KEYBOARD_EMULATED: return "Mouse and keyboard are now used as emulated devices.";
	case id::RSX_OVERLAYS_MOUSE_AND_KEYBOARD_PAD: return "Mouse and keyboard are now used as pad.";

	case id::HOME_MENU_TITLE: return "Home Menu";
	case id::HOME_MENU_EXIT_GAME: return "Exit Game";
	case id::HOME_MENU_RESTART: return "Restart Game";
	case id::HOME_MENU_RESUME: return "Resume Game";
	case id::HOME_MENU_FRIENDS: return "Friends";
	case id::HOME_MENU_FRIENDS_REQUESTS: return "Pending Friend Requests";
	case id::HOME_MENU_FRIENDS_GAME_INVITES: return "Game Invitations";
	case id::HOME_MENU_FRIENDS_BLOCKED: return "Blocked Users";
	case id::HOME_MENU_FRIENDS_STATUS_ONLINE: return "Online";
	case id::HOME_MENU_FRIENDS_STATUS_OFFLINE: return "Offline";
	case id::HOME_MENU_FRIENDS_STATUS_BLOCKED: return "Blocked";
	case id::HOME_MENU_FRIENDS_REQUEST_SENT: return "You sent a friend request";
	case id::HOME_MENU_FRIENDS_REQUEST_RECEIVED: return "Sent you a friend request";
	case id::HOME_MENU_FRIENDS_BLOCK_USER_MSG: return "Block this user?\n\n%0";
	case id::HOME_MENU_FRIENDS_UNBLOCK_USER_MSG: return "Unblock this user?\n\n%0";
	case id::HOME_MENU_FRIENDS_REMOVE_USER_MSG: return "Remove this user?\n\n%0";
	case id::HOME_MENU_FRIENDS_ACCEPT_REQUEST_MSG: return "Accept Request?\n\n%0";
	case id::HOME_MENU_FRIENDS_CANCEL_REQUEST_MSG: return "Cancel Request?\n\n%0";
	case id::HOME_MENU_FRIENDS_REJECT_REQUEST_MSG: return "Reject Request?\n\n%0";
	case id::HOME_MENU_FRIENDS_REJECT_REQUEST: return "Reject Request";
	case id::HOME_MENU_FRIENDS_ACCEPT_GAME_INVITE_MSG: return "Accept game invitation from %0?";
	case id::HOME_MENU_FRIENDS_REJECT_GAME_INVITE_MSG: return "Reject game invitation from %0?";
	case id::HOME_MENU_FRIENDS_REJECT_GAME_INVITE: return "Reject Invitation";
	case id::HOME_MENU_FRIENDS_NEXT_LIST: return "Next list";
	case id::HOME_MENU_SETTINGS: return "Settings";
	case id::HOME_MENU_SETTINGS_SAVE: return "Save custom configuration?";
	case id::HOME_MENU_SETTINGS_SAVE_BUTTON: return "Save";
	case id::HOME_MENU_SETTINGS_DISCARD: return "Discard the current settings' changes?";
	case id::HOME_MENU_SETTINGS_DISCARD_BUTTON: return "Discard";
	case id::HOME_MENU_SETTINGS_RESET_BUTTON: return "To default";
	case id::HOME_MENU_SETTINGS_AUDIO: return "Audio";
	case id::HOME_MENU_SETTINGS_AUDIO_MASTER_VOLUME: return "Master Volume";
	case id::HOME_MENU_SETTINGS_AUDIO_BACKEND: return "Audio Backend";
	case id::HOME_MENU_SETTINGS_AUDIO_BUFFERING: return "Enable Buffering";
	case id::HOME_MENU_SETTINGS_AUDIO_BUFFER_DURATION: return "Desired Audio Buffer Duration";
	case id::HOME_MENU_SETTINGS_AUDIO_TIME_STRETCHING: return "Enable Time Stretching";
	case id::HOME_MENU_SETTINGS_AUDIO_TIME_STRETCHING_THRESHOLD: return "Time Stretching Threshold";
	case id::HOME_MENU_SETTINGS_VIDEO: return "Video";
	case id::HOME_MENU_SETTINGS_VIDEO_VSYNC: return "VSync";
	case id::HOME_MENU_SETTINGS_VIDEO_FRAME_LIMIT: return "Frame Limit";
	case id::HOME_MENU_SETTINGS_VIDEO_ANISOTROPIC_OVERRIDE: return "Anisotropic Filter Override";
	case id::HOME_MENU_SETTINGS_VIDEO_OUTPUT_SCALING: return "Output Scaling";
	case id::HOME_MENU_SETTINGS_VIDEO_RCAS_SHARPENING: return "FidelityFX CAS Sharpening Intensity";
	case id::HOME_MENU_SETTINGS_VIDEO_RESOLUTION_SCALE_PERCENT: return "Resolution Scale";
	case id::HOME_MENU_SETTINGS_VIDEO_RESOLUTION_SCALE_THRESHOLD: return "Resolution Scale Threshold";
	case id::HOME_MENU_SETTINGS_VIDEO_STRETCH_TO_DISPLAY: return "Stretch To Display Area";
	case id::HOME_MENU_SETTINGS_VIDEO_STEREO_MODE: return "Stereo Mode";
	case id::HOME_MENU_SETTINGS_INPUT: return "Input";
	case id::HOME_MENU_SETTINGS_INPUT_BACKGROUND_INPUT: return "Background Input Enabled";
	case id::HOME_MENU_SETTINGS_INPUT_KEEP_PADS_CONNECTED: return "Keep Pads Connected";
	case id::HOME_MENU_SETTINGS_INPUT_SHOW_PS_MOVE_CURSOR: return "Show PS Move Cursor";
	case id::HOME_MENU_SETTINGS_INPUT_CAMERA_FLIP: return "Camera Flip";
	case id::HOME_MENU_SETTINGS_INPUT_PAD_MODE: return "Pad Handler Mode";
	case id::HOME_MENU_SETTINGS_INPUT_PAD_SLEEP: return "Pad Handler Sleep";
	case id::HOME_MENU_SETTINGS_INPUT_FAKE_MOVE_ROTATION_CONE_H: return "Fake PS Move Rotation Cone (Horizontal)";
	case id::HOME_MENU_SETTINGS_INPUT_FAKE_MOVE_ROTATION_CONE_V: return "Fake PS Move Rotation Cone (Vertical)";
	case id::HOME_MENU_SETTINGS_ADVANCED: return "Advanced";
	case id::HOME_MENU_SETTINGS_ADVANCED_PREFERRED_SPU_THREADS: return "Preferred SPU Threads";
	case id::HOME_MENU_SETTINGS_ADVANCED_MAX_CPU_PREEMPTIONS: return "Max Power Saving CPU-Preemptions";
	case id::HOME_MENU_SETTINGS_ADVANCED_ACCURATE_RSX_RESERVATION_ACCESS: return "Accurate RSX reservation access";
	case id::HOME_MENU_SETTINGS_ADVANCED_SLEEP_TIMERS_ACCURACY: return "Sleep Timers Accuracy";
	case id::HOME_MENU_SETTINGS_ADVANCED_RSX_MEMORY_TILING: return "Handle RSX Memory Tiling";
	case id::HOME_MENU_SETTINGS_ADVANCED_MAX_SPURS_THREADS: return "Max SPURS Threads";
	case id::HOME_MENU_SETTINGS_ADVANCED_DRIVER_WAKE_UP_DELAY: return "Driver Wake-Up Delay";
	case id::HOME_MENU_SETTINGS_ADVANCED_VBLANK_FREQUENCY: return "VBlank Frequency";
	case id::HOME_MENU_SETTINGS_ADVANCED_VBLANK_NTSC: return "VBlank NTSC Fixup";
	case id::HOME_MENU_SETTINGS_OVERLAYS: return "Overlays";
	case id::HOME_MENU_SETTINGS_OVERLAYS_SHOW_TROPHY_POPUPS: return "Show Trophy Popups";
	case id::HOME_MENU_SETTINGS_OVERLAYS_SHOW_RPCN_POPUPS: return "Show RPCN Popups";
	case id::HOME_MENU_SETTINGS_OVERLAYS_SHOW_SHADER_COMPILATION_HINT: return "Show Shader Compilation Hint";
	case id::HOME_MENU_SETTINGS_OVERLAYS_SHOW_PPU_COMPILATION_HINT: return "Show PPU Compilation Hint";
	case id::HOME_MENU_SETTINGS_OVERLAYS_SHOW_AUTO_SAVE_LOAD_HINT: return "Show Autosave/Autoload Hint";
	case id::HOME_MENU_SETTINGS_OVERLAYS_SHOW_PRESSURE_INTENSITY_TOGGLE_HINT: return "Show Pressure Intensity Toggle Hint";
	case id::HOME_MENU_SETTINGS_OVERLAYS_SHOW_ANALOG_LIMITER_TOGGLE_HINT: return "Show Analog Limiter Toggle Hint";
	case id::HOME_MENU_SETTINGS_OVERLAYS_SHOW_MOUSE_AND_KB_TOGGLE_HINT: return "Show Mouse And Keyboard Toggle Hint";
	case id::HOME_MENU_SETTINGS_OVERLAYS_SHOW_FATAL_ERROR_HINTS: return "Show Fatal Error Hints";
	case id::HOME_MENU_SETTINGS_OVERLAYS_RECORD_WITH_OVERLAYS: return "Record With Overlays";
	case id::HOME_MENU_SETTINGS_OVERLAYS_PLAY_MUSIC_DURING_BOOT: return "Play music during boot sequence.";
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY: return "Performance Overlay";
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_ENABLE: return "Enable Performance Overlay";
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_ENABLE_FRAMERATE_GRAPH: return "Enable Framerate Graph";
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_ENABLE_FRAMETIME_GRAPH: return "Enable Frametime Graph";
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_DETAIL_LEVEL: return "Detail level";
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_FRAMERATE_DETAIL_LEVEL: return "Framerate Graph Detail Level";
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_FRAMETIME_DETAIL_LEVEL: return "Frametime Graph Detail Level";
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_FRAMERATE_DATAPOINT_COUNT: return "Framerate Datapoints";
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_FRAMETIME_DATAPOINT_COUNT: return "Frametime Datapoints";
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_UPDATE_INTERVAL: return "Metrics Update Interval";
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_POSITION: return "Position";
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_CENTER_X: return "Center Horizontally";
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_CENTER_Y: return "Center Vertically";
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_MARGIN_X: return "Horizontal Margin";
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_MARGIN_Y: return "Vertical Margin";
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_FONT_SIZE: return "Font Size";
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_OPACITY: return "Opacity";
	case id::HOME_MENU_SETTINGS_PERFORMANCE_OVERLAY_USE_WINDOW_SPACE: return "Use Window Space";
	case id::HOME_MENU_SETTINGS_DEBUG: return "Debug";
	case id::HOME_MENU_SETTINGS_DEBUG_OVERLAY: return "Debug Overlay";
	case id::HOME_MENU_SETTINGS_DEBUG_INPUT_OVERLAY: return "Input Debug Overlay";
	case id::HOME_MENU_SETTINGS_MOUSE_DEBUG_INPUT_OVERLAY: return "Mouse Debug Overlay";
	case id::HOME_MENU_SETTINGS_DEBUG_DISABLE_VIDEO_OUTPUT: return "Disable Video Output";
	case id::HOME_MENU_SETTINGS_DEBUG_TEXTURE_LOD_BIAS: return "Texture LOD Bias Addend";
	case id::HOME_MENU_SCREENSHOT: return "Take Screenshot";
	case id::HOME_MENU_SAVESTATE: return "SaveState";
	case id::HOME_MENU_SAVESTATE_SAVE: return "Save Emulation State";
	case id::HOME_MENU_SAVESTATE_AND_EXIT: return "Save Emulation State And Exit";
	case id::HOME_MENU_RELOAD_SAVESTATE: return "Reload Last Emulation State";
	case id::HOME_MENU_RELOAD_SECOND_SAVESTATE: return "Reload Second-To-Last Emulation State";
	case id::HOME_MENU_RELOAD_THIRD_SAVESTATE: return "Reload Third-To-Last Emulation State";
	case id::HOME_MENU_RELOAD_FOURTH_SAVESTATE: return "Reload Fourth-To-Last Emulation State";
	case id::HOME_MENU_TOGGLE_FULLSCREEN: return "Toggle Fullscreen";
	case id::HOME_MENU_RECORDING: return "Start/Stop Recording";
	case id::HOME_MENU_TROPHIES: return "Trophies";
	case id::HOME_MENU_TROPHY_LIST_TITLE: return "Trophy Progress: %0";
	case id::HOME_MENU_TROPHY_LOCKED_TITLE: return "Locked trophy: %0";
	case id::HOME_MENU_TROPHY_HIDDEN_TITLE: return "Hidden trophy";
	case id::HOME_MENU_TROPHY_HIDDEN_DESCRIPTION: return "This trophy is hidden";
	case id::HOME_MENU_TROPHY_SHOW_HIDDEN_TROPHIES: return "Show hidden trophies";
	case id::HOME_MENU_TROPHY_HIDE_HIDDEN_TROPHIES: return "Hide hidden trophies";
	case id::HOME_MENU_TROPHY_PLATINUM_RELEVANT: return "Platinum relevant";
	case id::HOME_MENU_TROPHY_GRADE_BRONZE: return "Bronze";
	case id::HOME_MENU_TROPHY_GRADE_SILVER: return "Silver";
	case id::HOME_MENU_TROPHY_GRADE_GOLD: return "Gold";
	case id::HOME_MENU_TROPHY_GRADE_PLATINUM: return "Platinum";
	case id::HOME_MENU_TROPHY_SORT_GAME_DEFAULT: return "Sort: Game Default";
	case id::HOME_MENU_TROPHY_SORT_NOT_EARNED: return "Sort: Not Earned";
	case id::HOME_MENU_TROPHY_SORT_EARNED_DATE: return "Sort: Earned Date";
	case id::HOME_MENU_TROPHY_SORT_GRADE: return "Sort: Grade";

	case id::AUDIO_MUTED: return "Audio muted";
	case id::AUDIO_UNMUTED: return "Audio unmuted";
	case id::AUDIO_CHANGED: return "Volume changed to %0";
	case id::PROGRESS_DIALOG_PROGRESS: return "Progress:";
	case id::PROGRESS_DIALOG_PROGRESS_ANALYZING: return "Progress: analyzing...";
	case id::PROGRESS_DIALOG_REMAINING: return "remaining";
	case id::PROGRESS_DIALOG_DONE: return "done";
	case id::PROGRESS_DIALOG_FILE: return "file";
	case id::PROGRESS_DIALOG_MODULE: return "module";
	case id::PROGRESS_DIALOG_OF: return "of";
	case id::PROGRESS_DIALOG_PLEASE_WAIT: return "Please wait";
	case id::PROGRESS_DIALOG_STOPPING_PLEASE_WAIT: return "Stopping. Please wait...";
	case id::PROGRESS_DIALOG_SAVESTATE_PLEASE_WAIT: return "Creating savestate. Please wait...";
	case id::PROGRESS_DIALOG_SCANNING_PPU_EXECUTABLE: return "Scanning PPU Executable...";
	case id::PROGRESS_DIALOG_ANALYZING_PPU_EXECUTABLE: return "Analyzing PPU Executable...";
	case id::PROGRESS_DIALOG_SCANNING_PPU_MODULES: return "Scanning PPU Modules...";
	case id::PROGRESS_DIALOG_LOADING_PPU_MODULES: return "Loading PPU Modules...";
	case id::PROGRESS_DIALOG_COMPILING_PPU_MODULES: return "Compiling PPU Modules...";
	case id::PROGRESS_DIALOG_LINKING_PPU_MODULES: return "Linking PPU Modules...";
	case id::PROGRESS_DIALOG_APPLYING_PPU_CODE: return "Applying PPU Code...";
	case id::PROGRESS_DIALOG_BUILDING_SPU_CACHE: return "Building SPU Cache...";
	case id::EMULATION_PAUSED_RESUME_WITH_START: return "Press and hold the START button to resume";
	case id::EMULATION_RESUMING: return "Resuming...!";
	case id::EMULATION_FROZEN: return "The PS3 application has likely crashed, you can close it.";
	default: return {};
	}
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

std::string localized_overlay_string(id value, std::string_view language_tag, const char* argument)
{
	// language_tag is intentionally part of this boundary even while the first
	// headless catalog is English-only. It comes from iOS (not the emulated PS3
	// setting), so additional locale tables can be added without another ABI or
	// frontend change.
	(void)language_tag;
	if (const std::string_view text = english_overlay_string(value); !text.empty())
	{
		return substitute_argument(text, argument);
	}
	return argument ? std::string{argument} : std::string{};
}

std::u32string localized_overlay_u32string(id value, std::string_view language_tag, const char* argument)
{
	return utf8_to_u32(localized_overlay_string(value, language_tag, argument));
}
}
