#include "RPCS3IOS.h"
#include "RPCS3IOSCapabilities.h"
#include "RPCS3IOSContract.h"
#include "RPCS3IOSDisplay.h"
#include "RPCS3IOSLocalization.h"
#include "RPCS3IOSOverlayMedia.h"
#include "RPCS3IOSPath.h"
#include "RPCS3IOSPlatform.h"
#include "RPCS3IOSPerformance.h"
#include "RPCS3IOSRuntimePatches.h"
#include "RPCS3IOSResolution.h"
#include "RPCS3IOSSettings.h"
#include "RPCS3IOSZcullAccuracy.h"
#include "FirmwareInstaller.h"
#include "GameLibrary.h"
#include "IOSGSFrame.h"
#include "Emu/Io/IOS/IOSPadHandler.h"

#include "Emu/System.h"
#include "Emu/IdManager.h"
#include "Emu/Audio/IOS/IOSAudioBackend.h"
#include "Emu/Audio/Null/null_enumerator.h"
#include "Emu/Cell/Modules/cellMsgDialog.h"
#include "Emu/Cell/Modules/cellOskDialog.h"
#include "Emu/Cell/Modules/cellSaveData.h"
#include "Emu/Cell/Modules/sceNp.h"
#include "Emu/Cell/Modules/sceNpTrophy.h"
#include "Emu/Io/Null/NullKeyboardHandler.h"
#include "Emu/Io/Null/NullMouseHandler.h"
#include "Emu/Io/Null/null_camera_handler.h"
#include "Emu/Io/Null/null_music_handler.h"
#include "Emu/NP/rpcn_countries.h"
#include "Emu/NP/np_handler.h"
#include "Emu/NP/rpcn_client.h"
#include "Emu/NP/rpcn_config.h"
#ifdef HAVE_VULKAN
#include "Emu/RSX/VK/VKGSRender.h"
#endif
#include "Emu/system_config.h"
#include "Emu/system_progress.hpp"
#include "Emu/vfs_config.h"
#include "Input/pad_thread.h"
#include "Utilities/File.h"
#include "Utilities/JIT.h"
#include "Utilities/JITIOS.h"
#include "Utilities/StrFmt.h"
#include "util/logs.hpp"
#include "util/asm.hpp"
#include "util/video_source.h"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wextern-c-compat"
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
#include <wolfssl/openssl/evp.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#ifdef LLVM_AVAILABLE
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/TargetParser/Triple.h>
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace
{
std::mutex g_api_mutex;
std::mutex g_progress_mutex;
rpcs3::ios::lifecycle g_lifecycle;
rpcs3::ios::error_store g_last_error;
rpcs3_ios_config g_config{};
std::string g_application_support_path;
std::string g_cache_path;
std::string g_preferred_language = "en";
bool g_emu_started = false;
std::atomic_bool g_accept_display_surfaces = false;
std::atomic_bool g_accept_pad_state = false;
rpcs3::ios::display_surface_registry g_display_surface;
std::shared_ptr<rpcn::rpcn_client> g_rpcn_client;
bool g_rpcn_config_loaded = false;

struct boot_progress_snapshot
{
	std::string text;
	u32 files_total = 0;
	u32 files_done = 0;
	u64 file_bits_total = 0;
	u64 file_bits_known = 0;
	u32 modules_total = 0;
	u32 modules_done = 0;

	bool operator==(const boot_progress_snapshot&) const = default;
};

boot_progress_snapshot capture_boot_progress()
{
	auto read = []()
	{
		return boot_progress_snapshot{
			g_progr_text.operator std::string(),
			+g_progr_ftotal,
			+g_progr_fdone,
			+g_progr_ftotal_bits,
			+g_progr_fknown_bits,
			+g_progr_ptotal,
			+g_progr_pdone,
		};
	};

	boot_progress_snapshot snapshot = read();
	for (;;)
	{
		boot_progress_snapshot next = read();
		if (next == snapshot)
		{
			return snapshot;
		}
		snapshot = std::move(next);
	}
}

rpcs3_ios_emulation_state current_emulation_state() noexcept
{
	switch (Emu.GetStatus(false))
	{
	case system_state::stopped:
		return RPCS3_IOS_EMULATION_STATE_STOPPED;
	case system_state::loading:
		return RPCS3_IOS_EMULATION_STATE_LOADING;
	case system_state::ready:
		return RPCS3_IOS_EMULATION_STATE_READY;
	case system_state::starting:
		return RPCS3_IOS_EMULATION_STATE_STARTING;
	case system_state::running:
		return RPCS3_IOS_EMULATION_STATE_RUNNING;
	case system_state::paused:
	case system_state::frozen:
		return RPCS3_IOS_EMULATION_STATE_PAUSED;
	case system_state::stopping:
		return RPCS3_IOS_EMULATION_STATE_STOPPING;
	}

	return RPCS3_IOS_EMULATION_STATE_UNKNOWN;
}

bool wait_for_emulation_stop() noexcept
{
	constexpr auto interval = std::chrono::milliseconds(5);
	constexpr u32 attempts = 2'000;
	for (u32 attempt = 0; attempt < attempts; attempt++)
	{
		if (Emu.GetStatus(false) == system_state::stopped)
		{
			return true;
		}
		std::this_thread::sleep_for(interval);
	}
	return Emu.GetStatus(false) == system_state::stopped;
}

void set_error(std::string message)
{
	g_last_error.set(std::move(message));
}

void emit_log(int32_t level, std::string_view message)
{
	if (!g_config.log_callback)
	{
		return;
	}

	const std::string terminated{message};
	g_config.log_callback(g_config.user_context, level, terminated.c_str());
}

class callback_log_listener final : public logs::listener
{
public:
	void log(u64, const logs::message& message, std::string_view prefix, std::string_view text) override
	{
		std::string formatted;
		if (!prefix.empty())
		{
			fmt::append(formatted, "%s: ", prefix);
		}
		formatted.append(text);
		emit_log(static_cast<int32_t>(static_cast<logs::level>(message)), formatted);
	}
};

std::unique_ptr<callback_log_listener> g_log_listener;

class settings_state_guard
{
public:
	settings_state_guard()
		: m_settings(g_cfg.to_string())
		, m_name(g_cfg.name)
	{
	}

	~settings_state_guard()
	{
		g_cfg.from_default();
		if (!g_cfg.from_string(m_settings))
		{
			emit_log(1, "Unable to restore the in-memory RPCS3 settings snapshot");
		}
		g_cfg.name = m_name;
	}

	settings_state_guard(const settings_state_guard&) = delete;
	settings_state_guard& operator=(const settings_state_guard&) = delete;

private:
	std::string m_settings;
	std::string m_name;
};

bool load_settings_for_api(std::string_view title_id, bool& has_custom_config)
{
	const auto result = rpcs3::ios::load_effective_settings(title_id, has_custom_config);
	if (result == rpcs3::ios::settings_load_error::none)
	{
		return true;
	}

	set_error(std::string{rpcs3::ios::settings_load_error_detail(result)});
	return false;
}

rpcs3_ios_status validate_game_settings_target(const char* title_id, rpcs3::ios::installed_game* installed_game = nullptr)
{
	if (!title_id || !title_id[0])
	{
		set_error("A game title ID is required");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	auto game = rpcs3::ios::find_installed_game(title_id);
	if (!game)
	{
		set_error(fmt::format("Installed game not found: %s", title_id));
		return RPCS3_IOS_GAME_NOT_FOUND;
	}
	if (installed_game)
	{
		*installed_game = std::move(*game);
	}
	return RPCS3_IOS_OK;
}

std::vector<std::string> supported_game_resolution_options(u32 resolution_flags)
{
	std::vector<std::string> options = g_cfg.video.resolution.to_list();
	rpcs3::ios::filter_game_resolution_options(options, resolution_flags);
	return options;
}

void normalize_current_game_resolution(u32 resolution_flags)
{
	const std::vector<std::string> options = supported_game_resolution_options(resolution_flags);
	if (std::ranges::find(options, g_cfg.video.resolution.to_string()) != options.end())
	{
		return;
	}

	const auto preferred = std::ranges::find(options, rpcs3::ios::default_game_resolution);
	const std::string_view fallback = preferred != options.end() ? *preferred : options.front();
	g_cfg.video.resolution.from_string(fallback);
}

constexpr std::string_view zcull_accuracy_key = "gpu.zcull_accuracy";

std::string zcull_accuracy_value(bool use_default)
{
	const auto enabled = [use_default](const cfg::_bool& entry)
	{
		return (use_default ? entry.def_to_string() : entry.to_string()) == "true";
	};

	return std::string{rpcs3::ios::zcull_accuracy_name({
		enabled(g_cfg.video.precise_zpass_count),
		enabled(g_cfg.video.relaxed_zcull_sync),
	})};
}

bool set_zcull_accuracy(std::string_view value)
{
	const auto state = rpcs3::ios::parse_zcull_accuracy(value);
	if (!state)
	{
		return false;
	}
	g_cfg.video.precise_zpass_count.from_string(state->precise ? "true" : "false");
	g_cfg.video.relaxed_zcull_sync.from_string(state->relaxed ? "true" : "false");
	return true;
}

void enumerate_current_settings(
	rpcs3_ios_setting_callback setting_callback,
	rpcs3_ios_setting_option_callback option_callback,
	void* user_context,
	rpcs3::ios::setting_context context,
	u32 game_resolution_flags = 0)
{
	for (const auto& setting : rpcs3::ios::settings_catalog())
	{
		if (!rpcs3::ios::setting_is_available(setting.scope, context))
		{
			continue;
		}

		const bool is_zcull_accuracy = setting.key == zcull_accuracy_key;
		const std::string value = is_zcull_accuracy
			? zcull_accuracy_value(false)
			: setting.entry->to_string();
		const std::string default_value = is_zcull_accuracy
			? zcull_accuracy_value(true)
			: setting.entry->def_to_string();
		std::vector<std::string> options;
		if (setting.kind == RPCS3_IOS_SETTING_CHOICE)
		{
			if (is_zcull_accuracy)
			{
				options = {"Precise", "Approximate", "Relaxed"};
			}
			else if (setting.key == "network.psn_country")
			{
				options.reserve(countries::g_countries.size());
				for (const auto& country : countries::g_countries)
				{
					options.emplace_back(country.ccode);
				}
			}
			else if (setting.key == "gpu.anisotropic_filter")
			{
				options = {"0", "2", "4", "8", "16"};
			}
			else if (setting.key == "advanced.mfc_shuffling")
			{
				options = {"0", "1"};
			}
			else
			{
				options = setting.entry->to_list();
			}
			std::erase_if(options, [&setting](const std::string& option)
			{
				return (setting.key == "advanced.fifo_accuracy" && option == "PS3") ||
					(setting.key == "network.psn_status" && option == "Simulated") ||
					(setting.key == "audio.format" && option == "Manual") ||
					(setting.key == "cpu.spu_xfloat_accuracy" && option == "Inaccurate") ||
					(setting.key == "gpu.resolution" && option.ends_with("i"));
			});
			if (setting.key == "gpu.resolution")
			{
				rpcs3::ios::filter_game_resolution_options(options, game_resolution_flags);
			}
		}

		const rpcs3_ios_setting_info info{
			sizeof(rpcs3_ios_setting_info),
			static_cast<uint32_t>(setting.kind),
			setting.key.data(),
			setting.category.data(),
			setting.section.data(),
			setting.name.data(),
			setting.description.data(),
			value.c_str(),
			default_value.c_str(),
			setting.minimum,
			setting.maximum,
			setting.step,
			static_cast<uint32_t>(options.size()),
			1u,
		};
		setting_callback(user_context, &info);

		if (!option_callback)
		{
			continue;
		}
		for (const std::string& option : options)
		{
			std::string label = option;
			if (setting.key == "network.psn_country")
			{
				const auto country = std::find_if(countries::g_countries.begin(), countries::g_countries.end(), [&option](const auto& entry)
				{
					return entry.ccode == option;
				});
				if (country != countries::g_countries.end())
				{
					label = country->name;
				}
			}
			else if (setting.key == "gpu.anisotropic_filter")
			{
				label = option == "0" ? "Auto" : option + "x";
			}
			else if (setting.key == "advanced.mfc_shuffling")
			{
				label = option == "0" ? "Disabled" : "Enabled";
			}
			const rpcs3_ios_setting_option setting_option{
				sizeof(rpcs3_ios_setting_option),
				0,
				setting.key.data(),
				option.c_str(),
				label.c_str(),
			};
			option_callback(user_context, &setting_option);
		}
	}
}

rpcs3_ios_status update_current_setting(
	const char* key,
	const char* value,
	rpcs3::ios::setting_context context)
{
	const auto* setting = rpcs3::ios::find_setting(key, context);
	if (!setting)
	{
		set_error(fmt::format("Unknown or unavailable iOS setting: %s", key));
		return RPCS3_IOS_SETTING_NOT_FOUND;
	}

	if (setting->key == "network.psn_country" && std::none_of(countries::g_countries.begin(), countries::g_countries.end(), [value](const auto& country)
	{
		return country.ccode == value;
	}))
	{
		set_error(fmt::format("Invalid PSN country code '%s'", value));
		return RPCS3_IOS_SETTING_INVALID;
	}
	if (setting->key == zcull_accuracy_key)
	{
		if (!set_zcull_accuracy(value))
		{
			set_error(fmt::format("Invalid value '%s' for setting '%s'", value, key));
			return RPCS3_IOS_SETTING_INVALID;
		}
		return RPCS3_IOS_OK;
	}

	const std::string previous = setting->entry->to_string();
	if (!setting->entry->from_string(value))
	{
		setting->entry->from_string(previous);
		set_error(fmt::format("Invalid value '%s' for setting '%s'", value, key));
		return RPCS3_IOS_SETTING_INVALID;
	}
	return RPCS3_IOS_OK;
}

void reset_current_settings(rpcs3::ios::setting_context context)
{
	for (const auto& setting : rpcs3::ios::settings_catalog())
	{
		if (rpcs3::ios::setting_is_available(setting.scope, context))
		{
			if (setting.key == zcull_accuracy_key)
			{
				g_cfg.video.precise_zpass_count.from_default();
				g_cfg.video.relaxed_zcull_sync.from_default();
			}
			else
			{
				setting.entry->from_default();
			}
		}
	}
}

struct main_thread_payload
{
	std::function<void()> function;
	atomic_t<u32>* wake_up = nullptr;
};

void invoke_main_thread_payload(void* raw_payload)
{
	std::unique_ptr<main_thread_payload> payload{static_cast<main_thread_payload*>(raw_payload)};
	payload->function();
	if (payload->wake_up)
	{
		*payload->wake_up = true;
		payload->wake_up->notify_one();
	}
}

EmuCallbacks make_callbacks()
{
	EmuCallbacks callbacks{};

	callbacks.call_from_main_thread = [](std::function<void()> function, atomic_t<u32>* wake_up)
	{
		if (!g_config.main_thread_callback)
		{
			function();
			if (wake_up)
			{
				*wake_up = true;
				wake_up->notify_one();
			}
			return;
		}

		auto* payload = new main_thread_payload{std::move(function), wake_up};
		g_config.main_thread_callback(g_config.user_context, &invoke_main_thread_payload, payload);
	};

	callbacks.on_run = [](bool) {};
	callbacks.on_pause = []() {};
	callbacks.on_resume = []() {};
	callbacks.on_stop = []() {};
	callbacks.on_ready = []() {};
	callbacks.on_missing_fw = []() { emit_log(5, "PlayStation 3 firmware is not installed yet"); };
	callbacks.on_emulation_stop_no_response = [](std::shared_ptr<atomic_t<bool>>, int)
	{
		emit_log(2, "Emulation shutdown did not respond in time");
	};
	callbacks.on_save_state_progress = [](std::shared_ptr<atomic_t<bool>>, stx::shared_ptr<utils::serial>, stx::atomic_ptr<std::string>*, std::shared_ptr<void>) {};
	callbacks.enable_disc_eject = [](bool) {};
	callbacks.enable_disc_insert = [](bool) {};
	callbacks.try_to_quit = [](bool, std::function<void()> on_exit)
	{
		if (on_exit)
		{
			on_exit();
		}
		return true;
	};
	callbacks.handle_taskbar_progress = [](s32, s32) {};
	callbacks.init_kb_handler = []()
	{
		ensure(g_fxo->init<KeyboardHandlerBase, NullKeyboardHandler>(Emu.DeserialManager()));
	};
	callbacks.init_mouse_handler = []()
	{
		ensure(g_fxo->init<MouseHandlerBase, NullMouseHandler>(Emu.DeserialManager()));
	};
	callbacks.init_pad_handler = [](std::string_view title_id)
	{
		ensure(g_fxo->init<named_thread<pad_thread>>(nullptr, nullptr, title_id));
	};
	callbacks.update_emu_settings = []() {};
	callbacks.save_emu_settings = []()
	{
		Emulator::SaveSettings(g_cfg.to_string(), Emu.GetTitleID());
	};
	callbacks.close_gs_frame = []() {};
	callbacks.get_gs_frame = []() -> std::unique_ptr<GSFrameBase>
	{
		if (!g_display_surface.snapshot().valid())
		{
			emit_log(1, "RPCS3 requested a graphics frame without an attached iOS display surface");
			return {};
		}
		return std::make_unique<rpcs3::ios::gs_frame>(g_display_surface);
	};
	callbacks.get_camera_handler = []() -> std::shared_ptr<camera_handler_base>
	{
		return std::make_shared<null_camera_handler>();
	};
	callbacks.get_music_handler = []() -> std::shared_ptr<music_handler_base>
	{
		return std::make_shared<null_music_handler>();
	};
	callbacks.init_gs_render = [](utils::serial* archive)
	{
		switch (g_cfg.video.renderer.get())
		{
		case video_renderer::vulkan:
#ifdef HAVE_VULKAN
			g_fxo->init<rsx::thread, named_thread<VKGSRender>>(archive);
			break;
#else
			fmt::throw_exception("The iOS core was built without Vulkan support");
#endif
		case video_renderer::null:
			fmt::throw_exception("The iOS video frontend requires the Vulkan renderer");
		default:
			fmt::throw_exception("Unsupported iOS video renderer: %s", g_cfg.video.renderer.get());
		}
	};
	callbacks.get_audio = []() -> std::shared_ptr<AudioBackend>
	{
		return std::make_shared<IOSAudioBackend>();
	};
	callbacks.get_audio_enumerator = [](u64) -> std::shared_ptr<audio_device_enumerator>
	{
		return std::make_shared<null_enumerator>();
	};
	callbacks.get_msg_dialog = []() -> std::shared_ptr<MsgDialogBase> { return {}; };
	callbacks.get_osk_dialog = []() -> std::shared_ptr<OskDialogBase> { return {}; };
	callbacks.get_save_dialog = []() -> std::unique_ptr<SaveDialogBase> { return {}; };
	callbacks.get_sendmessage_dialog = []() -> std::shared_ptr<SendMessageDialogBase> { return {}; };
	callbacks.get_recvmessage_dialog = []() -> std::shared_ptr<RecvMessageDialogBase> { return {}; };
	callbacks.get_trophy_notification_dialog = []() -> std::unique_ptr<TrophyNotificationBase> { return {}; };
	callbacks.get_localized_string = [](localized_string_id id, const char* argument)
	{
		return rpcs3::ios::localized_overlay_string(id, g_preferred_language, argument);
	};
	callbacks.get_localized_u32string = [](localized_string_id id, const char* argument)
	{
		return rpcs3::ios::localized_overlay_u32string(id, g_preferred_language, argument);
	};
	callbacks.get_localized_setting = [](const cfg::_base* setting, u32 enum_index)
	{
		if (!setting)
		{
			return std::string{};
		}
		const auto options = setting->to_list();
		if (enum_index >= options.size())
		{
			return std::string{};
		}
		return rpcs3::ios::localized_setting_string(
			setting->get_name(), enum_index, options[enum_index], g_preferred_language);
	};
	callbacks.get_photo_path = [](std::string_view name)
	{
		return fs::get_config_dir() + "photos/" + std::string{name};
	};
	callbacks.play_sound = [](const std::string&, std::optional<f32>) {};
	callbacks.get_image_info = [](const std::string&, std::string&, s32&, s32&, s32&) { return false; };
	callbacks.get_scaled_image = [](const std::string&, s32, s32, s32&, s32&, u8*, bool) { return false; };
	callbacks.resolve_path = [](std::string_view path)
	{
		return rpcs3::ios::normalize_resolved_host_path(path);
	};
	callbacks.get_font_dirs = []() { return std::vector<std::string>{}; };
	callbacks.on_install_pkgs = [](const std::vector<std::string>&) { return false; };
	callbacks.add_breakpoint = [](u32) {};
	callbacks.display_sleep_control_supported = []() { return rpcs3::ios::display_sleep_control_supported(); };
	callbacks.enable_display_sleep = [](bool enable) { rpcs3::ios::enable_display_sleep(enable); };
	callbacks.check_microphone_permissions = []() {};
	callbacks.make_video_source = []() { return rpcs3::ios::make_overlay_media_source(); };
	callbacks.enable_gamemode = [](bool) {};
	callbacks.get_database_config = [](const std::string&) { return std::string{}; };
	return callbacks;
}

rpcs3_ios_status validate_config(const rpcs3_ios_config* config)
{
	if (const auto result = rpcs3::ios::validate_config_contract(config); result != RPCS3_IOS_OK)
	{
		set_error("Invalid ABI version, structure size, or sandbox path");
		return result;
	}

	return RPCS3_IOS_OK;
}

void load_rpcn_config()
{
	if (!g_rpcn_config_loaded)
	{
		g_cfg_rpcn.load();
		std::string legacy_password = g_cfg_rpcn.get_password();
		std::string legacy_token = g_cfg_rpcn.get_token();
		if (!legacy_password.empty() || !legacy_token.empty())
		{
			g_cfg_rpcn.set_password({});
			g_cfg_rpcn.set_token({});
			if (!g_cfg_rpcn.save())
			{
				emit_log(1, "Could not scrub legacy RPCN credentials from rpcn.yml");
			}
			g_cfg_rpcn.set_runtime_credentials(std::move(legacy_password), std::move(legacy_token));
			emit_log(4, "Migrated legacy RPCN credentials to process-lifetime memory");
		}
		g_rpcn_config_loaded = true;
	}
}

bool valid_rpcn_username(std::string_view username)
{
	return username.size() >= 3 && username.size() <= 16 &&
		std::ranges::all_of(username, [](unsigned char value)
		{
			return std::isalnum(value) || value == '-' || value == '_';
		});
}

bool valid_rpcn_email(std::string_view email)
{
	if (email.empty() || email.size() > 254 || email.find(' ') != std::string_view::npos ||
		email.find('\t') != std::string_view::npos)
	{
		return false;
	}
	const auto separator = email.find('@');
	return separator != std::string_view::npos && separator > 0 && separator + 1 < email.size() &&
		email.find('@', separator + 1) == std::string_view::npos;
}

bool valid_rpcn_token(std::string_view token)
{
	return token.empty() || (token.size() == 16 && std::ranges::all_of(token, [](unsigned char value)
	{
		return std::isdigit(value) || (value >= 'A' && value <= 'Z');
	}));
}

std::optional<std::string> derive_rpcn_password(std::string_view password)
{
	if (password.empty() || password.size() > 1024)
	{
		return std::nullopt;
	}

	constexpr std::string_view salt = "No matter where you go, everybody's connected.";
	std::array<u8, SHA3_256_DIGEST_LENGTH> digest{};
	if (wc_PBKDF2(digest.data(), reinterpret_cast<const u8*>(password.data()), ::narrow<s32>(password.size()),
		reinterpret_cast<const u8*>(salt.data()), ::narrow<s32>(salt.size()), 200'000,
		SHA3_256_DIGEST_LENGTH, WC_SHA3_256) != 0)
	{
		return std::nullopt;
	}

	constexpr std::string_view hex = "0123456789ABCDEF";
	std::string derived(SHA3_256_DIGEST_LENGTH * 2, '0');
	for (usz index = 0; index < digest.size(); index++)
	{
		derived[index * 2] = hex[digest[index] >> 4];
		derived[index * 2 + 1] = hex[digest[index] & 0x0f];
	}
	return derived;
}

rpcs3_ios_status validate_rpcn_operation(bool require_stopped)
{
	if (g_lifecycle.state() != RPCS3_IOS_STATE_READY)
	{
		set_error("RPCS3Core must be ready before managing RPCN");
		return RPCS3_IOS_INVALID_STATE;
	}
	if (require_stopped && current_emulation_state() != RPCS3_IOS_EMULATION_STATE_STOPPED)
	{
		set_error("Stop emulation before changing RPCN account or server information");
		return RPCS3_IOS_INVALID_STATE;
	}
	load_rpcn_config();
	return RPCS3_IOS_OK;
}

std::string rpcn_error_detail(rpcn::ErrorType error)
{
	switch (error)
	{
	case rpcn::ErrorType::NoError: return {};
	case rpcn::ErrorType::Malformed: return "The RPCN server returned a malformed response";
	case rpcn::ErrorType::Invalid: return "The RPCN server does not support this operation";
	case rpcn::ErrorType::InvalidInput: return "RPCN rejected the supplied input";
	case rpcn::ErrorType::TooSoon: return "RPCN rate-limited this operation; try again later";
	case rpcn::ErrorType::LoginError: return "The RPCN username or credential is invalid";
	case rpcn::ErrorType::LoginAlreadyLoggedIn: return "The RPCN account is already logged in";
	case rpcn::ErrorType::LoginInvalidUsername: return "The RPCN username is invalid";
	case rpcn::ErrorType::LoginInvalidPassword: return "The RPCN password is invalid";
	case rpcn::ErrorType::LoginInvalidToken: return "The RPCN verification token is invalid";
	case rpcn::ErrorType::CreationExistingUsername: return "An RPCN account already uses that username";
	case rpcn::ErrorType::CreationBannedEmailProvider: return "The RPCN server does not accept that email provider";
	case rpcn::ErrorType::CreationExistingEmail: return "An RPCN account already uses that email address";
	case rpcn::ErrorType::CreationError: return "RPCN could not create the account";
	case rpcn::ErrorType::Unauthorized: return "The RPCN account is not authorized for this operation";
	case rpcn::ErrorType::DbFail: return "The RPCN server database operation failed";
	case rpcn::ErrorType::EmailFail: return "The RPCN server could not send the email";
	case rpcn::ErrorType::NotFound: return "The RPCN user was not found";
	case rpcn::ErrorType::Blocked: return "One of these RPCN users has blocked the other";
	case rpcn::ErrorType::AlreadyFriend: return "That RPCN user is already a friend";
	case rpcn::ErrorType::Unsupported: return "The RPCN server does not support this operation";
	default: return fmt::format("RPCN rejected the operation (error %u)", static_cast<u8>(error));
	}
}

std::shared_ptr<rpcn::rpcn_client> rpcn_connection(bool reconnect)
{
	if (!g_rpcn_client)
	{
		g_rpcn_client = rpcn::rpcn_client::get_instance(0);
	}
	else if (reconnect)
	{
		g_rpcn_client->reconnect();
	}
	return g_rpcn_client;
}

void prepare_rpcn_for_guest_boot()
{
	// The iOS RPCN manager deliberately retains the singleton across sessions.
	// Keep its signaling worker asleep while BootGame rebuilds fixed objects;
	// np_handler activates it only after the guest P2P context is initialized.
	if (g_rpcn_client)
	{
		g_rpcn_client->set_guest_signaling_active(false);
	}
}

rpcs3_ios_status connect_rpcn(bool authenticate, bool reconnect)
{
	auto client = rpcn_connection(reconnect);
	if (const auto state = client->wait_for_connection(); state != rpcn::rpcn_state::failure_no_failure)
	{
		set_error("Unable to connect to RPCN: " + rpcn::rpcn_state_to_string(state));
		return RPCS3_IOS_RPCN_ERROR;
	}
	if (authenticate)
	{
		if (g_cfg_rpcn.get_npid().empty() || g_cfg_rpcn.get_password().empty())
		{
			set_error("Configure an RPCN username and password first");
			return RPCS3_IOS_RPCN_NOT_CONFIGURED;
		}
		if (const auto state = client->wait_for_authentified(); state != rpcn::rpcn_state::failure_no_failure)
		{
			set_error("Unable to authenticate with RPCN: " + rpcn::rpcn_state_to_string(state));
			return RPCS3_IOS_RPCN_ERROR;
		}
	}
	return RPCS3_IOS_OK;
}

bool persist_rpcn_profile(std::string_view username, std::string password, std::string token, bool ipv6_support)
{
	// Remove legacy plaintext credentials before saving public RPCN metadata.
	g_cfg_rpcn.set_npid(username);
	g_cfg_rpcn.set_password({});
	g_cfg_rpcn.set_token({});
	g_cfg_rpcn.set_ipv6_support(ipv6_support);
	if (!g_cfg_rpcn.save())
	{
		return false;
	}
	g_cfg_rpcn.set_runtime_credentials(std::move(password), std::move(token));
	return true;
}
}

extern "C" uint32_t rpcs3_ios_abi_version(void) noexcept
{
	return RPCS3_IOS_ABI_VERSION;
}

extern "C" const char* rpcs3_ios_build_info(void) noexcept
{
	return "{\"abi\":19,\"frontend\":\"ios\",\"upstream\":\"3d587726a23f514be0e7c3ac43e2db0cf2fe931a\",\"llvm\":\"ca7933e47d3a3451d81e72ac174dcb5aa28b59d1\",\"jit\":\"sealed-arena\",\"renderer\":\"vulkan-moltenvk\",\"moltenvk\":\"1.4.2\",\"ffmpeg\":\"8.1.1\",\"audio\":\"remoteio\",\"input\":\"gamecontroller-multiplayer-rumble\",\"games\":\"pkg-iso-zip-folder-updates-runtime-patches-library\",\"settings\":\"global-and-per-game-cfg-root-catalog\",\"rpcn\":\"servers-account-social-online\",\"performance\":\"fps-cpu-rsx-memory\",\"lifecycle\":\"pause-resume-stop\",\"media_codecs\":true}";
}

extern "C" rpcs3_ios_status rpcs3_ios_initialize(const rpcs3_ios_config* config) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (g_lifecycle.state() != RPCS3_IOS_STATE_UNINITIALIZED)
	{
		set_error("RPCS3Core initialization is single-instance");
		return RPCS3_IOS_INVALID_STATE;
	}

	if (const auto result = validate_config(config); result != RPCS3_IOS_OK)
	{
		return result;
	}
	if (const auto result = g_lifecycle.begin_initialize(); result != RPCS3_IOS_OK)
	{
		set_error("RPCS3Core initialization state transition failed");
		return result;
	}

	if (!rpcs3::ios::jit::is_ready())
	{
		set_error(rpcs3::ios::jit::last_error());
		g_lifecycle.finish_initialize(false);
		return RPCS3_IOS_JIT_UNAVAILABLE;
	}
	if (!rpcs3::ios::jit::prepare_arena() || !rpcs3::ios::jit::seal_arena())
	{
		set_error(rpcs3::ios::jit::last_error());
		g_lifecycle.finish_initialize(false);
		return RPCS3_IOS_JIT_MAPPING_FAILED;
	}

	try
	{
		g_application_support_path = config->application_support_path;
		g_cache_path = config->cache_path;
		g_config = *config;
		g_config.application_support_path = g_application_support_path.c_str();
		g_config.cache_path = g_cache_path.c_str();
		g_rpcn_config_loaded = false;
		g_rpcn_client.reset();

		if (!fs::set_config_dir(g_config.application_support_path) || !fs::set_cache_dir(g_config.cache_path))
		{
			set_error("Unable to configure writable RPCS3 sandbox directories");
			g_lifecycle.finish_initialize(false);
			return RPCS3_IOS_INVALID_ARGUMENT;
		}

		g_log_listener = std::make_unique<callback_log_listener>();
		logs::listener::add(g_log_listener.get());
		g_preferred_language = rpcs3::ios::preferred_language_identifier();
		rpcs3::ios::set_localization_resolver(&rpcs3::ios::localized_application_string);
		emit_log(4, "Using iOS preferred language for native overlays: " + g_preferred_language);
		rpcs3::ios::shared_pad_states().clear();
		rpcs3::ios::shared_pad_feedback().clear();
		const auto jit_stats = rpcs3::ios::jit::get_statistics();
		emit_log(4, fmt::format(
			"Prepared and sealed a %u MiB Universal JIT arena in %u bounded command-1 chunks; StikDebug may now disconnect",
			jit_stats.capacity / (1024 * 1024), jit_stats.preparation_chunks));
		Emu.SetCallbacks(make_callbacks());
		Emu.SetSupportedRenderers({video_renderer::vulkan});
		Emu.SetDefaultRenderer(video_renderer::vulkan);
		// The configured name only satisfies RPCS3's pre-init invariant. The
		// renderer falls back to MoltenVK's first enumerated physical device.
		Emu.SetDefaultGraphicsAdapter("iOS Metal GPU");
		Emu.SetHasGui(false);
		Emu.SetHeadless(false);
		Emu.SetUsr("00000001");
		g_emu_started = true;
		Emu.Init();
		g_lifecycle.finish_initialize(true);
		g_accept_display_surfaces = true;
		g_accept_pad_state = true;
		emit_log(4, "RPCS3 Emu.Init completed with the iOS Vulkan/MoltenVK and RemoteIO frontend");
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception during Emu.Init");
	}

	g_accept_display_surfaces = false;
	g_accept_pad_state = false;
	rpcs3::ios::shared_pad_states().clear();
	rpcs3::ios::shared_pad_feedback().clear();
	g_lifecycle.finish_initialize(false);
	return RPCS3_IOS_CORE_INIT_FAILED;
}

extern "C" rpcs3_ios_status rpcs3_ios_run_llvm_self_test(uint64_t input, uint64_t* output) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (!output)
	{
		set_error("The self-test output pointer is null");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	if (g_lifecycle.state() != RPCS3_IOS_STATE_READY)
	{
		set_error("RPCS3Core must be ready before running the LLVM self-test");
		return RPCS3_IOS_INVALID_STATE;
	}
	if (current_emulation_state() != RPCS3_IOS_EMULATION_STATE_STOPPED)
	{
		set_error("Stop emulation before running the LLVM self-test");
		return RPCS3_IOS_INVALID_STATE;
	}

#ifdef LLVM_AVAILABLE
	try
	{
		jit_compiler compiler({}, "generic");
		auto module = std::make_unique<llvm::Module>("rpcs3_ios_self_test", compiler.get_context());
		module->setTargetTriple(llvm::Triple(jit_compiler::triple2()));
		auto* type = llvm::FunctionType::get(
			llvm::Type::getInt64Ty(compiler.get_context()),
			{llvm::Type::getInt64Ty(compiler.get_context())},
			false);
		auto* function = llvm::Function::Create(type, llvm::Function::ExternalLinkage, "rpcs3_ios_test_function", *module);
		auto* block = llvm::BasicBlock::Create(compiler.get_context(), "entry", function);
		llvm::IRBuilder<> builder(block);
		auto* argument = function->getArg(0);
		auto* multiplied = builder.CreateMul(argument, llvm::ConstantInt::get(argument->getType(), 3));
		builder.CreateRet(builder.CreateAdd(multiplied, llvm::ConstantInt::get(argument->getType(), 7)));

		std::string error;
		if (!compiler.try_add(std::move(module), error) || !compiler.try_fin(error))
		{
			set_error("LLVM compilation failed: " + error);
			return RPCS3_IOS_SELF_TEST_FAILED;
		}

		using test_function = uint64_t (*)(uint64_t);
		const auto function_address = compiler.get("rpcs3_ios_test_function");
		if (!function_address)
		{
			set_error("LLVM did not publish the self-test function");
			return RPCS3_IOS_SELF_TEST_FAILED;
		}

		const uint64_t actual = reinterpret_cast<test_function>(function_address)(input);
		const uint64_t expected = input * 3 + 7;
		if (actual != expected)
		{
			set_error(fmt::format("LLVM self-test mismatch: expected %u, received %u", expected, actual));
			return RPCS3_IOS_SELF_TEST_FAILED;
		}

		*output = actual;
		emit_log(4, "RPCS3 LLVM JIT self-test passed");
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception during the LLVM self-test");
	}
#else
	(void)input;
	set_error("RPCS3Core was built without LLVM");
#endif

	return RPCS3_IOS_SELF_TEST_FAILED;
}

extern "C" const char* rpcs3_ios_firmware_version(void) noexcept
{
	thread_local std::string copy;
	std::lock_guard lock(g_api_mutex);
	copy.clear();
	try
	{
		copy = rpcs3::ios::firmware_version();
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while reading the installed firmware version");
	}
	return copy.c_str();
}

extern "C" rpcs3_ios_status rpcs3_ios_install_firmware(
	const char* pup_path,
	rpcs3_ios_firmware_progress_callback progress_callback,
	void* user_context) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (!pup_path || !pup_path[0] || pup_path[0] != '/')
	{
		set_error("The firmware path must be an absolute sandbox path");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	if (g_lifecycle.state() != RPCS3_IOS_STATE_READY ||
		current_emulation_state() != RPCS3_IOS_EMULATION_STATE_STOPPED)
	{
		set_error("Stop emulation before installing firmware");
		return RPCS3_IOS_INVALID_STATE;
	}
	if (const auto result = g_lifecycle.begin_content_install(); result != RPCS3_IOS_OK)
	{
		set_error("RPCS3Core must be ready before installing firmware");
		return result;
	}

	try
	{
		auto progress = [progress_callback, user_context](u32 completed, u32 total, std::string_view stage)
		{
			if (!progress_callback)
			{
				return;
			}
			const std::string terminated{stage};
			progress_callback(user_context, completed, total, terminated.c_str());
		};

		emit_log(4, "Starting PlayStation 3 firmware installation");
		const auto install_result = rpcs3::ios::install_firmware(pup_path, progress);
		g_lifecycle.finish_content_install();
		if (install_result.error != rpcs3::ios::firmware_install_error::none)
		{
			set_error(install_result.detail);
			emit_log(2, install_result.detail);
			return install_result.error == rpcs3::ios::firmware_install_error::invalid_firmware
				? RPCS3_IOS_FIRMWARE_INVALID
				: RPCS3_IOS_FIRMWARE_INSTALL_FAILED;
		}

		emit_log(4, fmt::format("Successfully installed PlayStation 3 firmware %s", install_result.version));
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception during firmware installation");
	}

	g_lifecycle.finish_content_install();
	return RPCS3_IOS_FIRMWARE_INSTALL_FAILED;
}

extern "C" rpcs3_ios_status rpcs3_ios_install_package(
	const char* package_path,
	rpcs3_ios_package_progress_callback progress_callback,
	void* user_context) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (!package_path || !package_path[0] || package_path[0] != '/')
	{
		set_error("The package path must be an absolute sandbox path");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	if (const auto result = rpcs3::ios::validate_idle_operation_contract(
		g_lifecycle.state(), current_emulation_state()); result != RPCS3_IOS_OK)
	{
		set_error("Stop emulation before installing a package");
		return result;
	}
	if (const auto result = g_lifecycle.begin_content_install(); result != RPCS3_IOS_OK)
	{
		set_error("RPCS3Core must be ready before installing a package");
		return result;
	}

	try
	{
		auto progress = [progress_callback, user_context](u32 completed, u32 total, std::string_view stage)
		{
			if (!progress_callback)
			{
				return;
			}
			const std::string terminated{stage};
			progress_callback(user_context, completed, total, terminated.c_str());
		};

		emit_log(4, "Starting PlayStation 3 package installation");
		const auto install_result = rpcs3::ios::install_game_package(package_path, progress);
		g_lifecycle.finish_content_install();
		if (install_result.error != rpcs3::ios::game_package_install_error::none)
		{
			set_error(install_result.detail);
			emit_log(2, install_result.detail);
			return install_result.error == rpcs3::ios::game_package_install_error::invalid_package
				? RPCS3_IOS_PACKAGE_INVALID
				: RPCS3_IOS_PACKAGE_INSTALL_FAILED;
		}

		emit_log(4, fmt::format("Successfully installed PlayStation 3 package: %s (%s)",
			install_result.title, install_result.title_id));
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception during package installation");
	}

	g_lifecycle.finish_content_install();
	return RPCS3_IOS_PACKAGE_INSTALL_FAILED;
}

extern "C" rpcs3_ios_status rpcs3_ios_install_iso(
	const char* iso_path,
	const char* key_path,
	rpcs3_ios_iso_progress_callback progress_callback,
	void* user_context) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (!iso_path || !iso_path[0] || iso_path[0] != '/' ||
		(key_path && key_path[0] && key_path[0] != '/'))
	{
		set_error("ISO and key paths must be absolute sandbox paths");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	if (const auto result = rpcs3::ios::validate_idle_operation_contract(
		g_lifecycle.state(), current_emulation_state()); result != RPCS3_IOS_OK)
	{
		set_error("Stop emulation before installing an ISO");
		return result;
	}
	if (const auto result = g_lifecycle.begin_content_install(); result != RPCS3_IOS_OK)
	{
		set_error("RPCS3Core must be ready before installing an ISO");
		return result;
	}

	try
	{
		auto progress = [progress_callback, user_context](u32 completed, u32 total, std::string_view stage)
		{
			if (!progress_callback)
			{
				return;
			}
			const std::string terminated{stage};
			progress_callback(user_context, completed, total, terminated.c_str());
		};

		emit_log(4, "Starting PlayStation 3 ISO installation");
		const auto install_result = rpcs3::ios::install_game_iso(
			iso_path, key_path ? key_path : "", progress);
		g_lifecycle.finish_content_install();
		if (install_result.error != rpcs3::ios::game_iso_install_error::none)
		{
			set_error(install_result.detail);
			emit_log(2, install_result.detail);
			return install_result.error == rpcs3::ios::game_iso_install_error::invalid_iso
				? RPCS3_IOS_ISO_INVALID
				: RPCS3_IOS_ISO_INSTALL_FAILED;
		}

		emit_log(4, fmt::format("Successfully installed PlayStation 3 ISO: %s (%s)",
			install_result.title, install_result.title_id));
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception during ISO installation");
	}

	g_lifecycle.finish_content_install();
	return RPCS3_IOS_ISO_INSTALL_FAILED;
}

extern "C" rpcs3_ios_status rpcs3_ios_install_zip(
	const char* zip_path,
	rpcs3_ios_zip_progress_callback progress_callback,
	void* user_context) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (!zip_path || !zip_path[0] || zip_path[0] != '/')
	{
		set_error("The ZIP path must be an absolute sandbox path");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	if (const auto result = rpcs3::ios::validate_idle_operation_contract(
		g_lifecycle.state(), current_emulation_state()); result != RPCS3_IOS_OK)
	{
		set_error("Stop emulation before installing a ZIP");
		return result;
	}
	if (const auto result = g_lifecycle.begin_content_install(); result != RPCS3_IOS_OK)
	{
		set_error("RPCS3Core must be ready before installing a ZIP");
		return result;
	}

	try
	{
		auto progress = [progress_callback, user_context](u32 completed, u32 total, std::string_view stage)
		{
			if (!progress_callback)
			{
				return;
			}
			const std::string terminated{stage};
			progress_callback(user_context, completed, total, terminated.c_str());
		};

		emit_log(4, "Starting PlayStation 3 ZIP installation");
		const auto install_result = rpcs3::ios::install_game_zip(zip_path, progress);
		g_lifecycle.finish_content_install();
		if (install_result.error != rpcs3::ios::game_zip_install_error::none)
		{
			set_error(install_result.detail);
			emit_log(2, install_result.detail);
			return install_result.error == rpcs3::ios::game_zip_install_error::invalid_zip
				? RPCS3_IOS_ZIP_INVALID
				: RPCS3_IOS_ZIP_INSTALL_FAILED;
		}

		emit_log(4, fmt::format("Successfully installed PlayStation 3 ZIP: %s (%s)",
			install_result.title, install_result.title_id));
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception during ZIP installation");
	}

	g_lifecycle.finish_content_install();
	return RPCS3_IOS_ZIP_INSTALL_FAILED;
}

extern "C" rpcs3_ios_status rpcs3_ios_install_folder(
	const char* folder_path,
	rpcs3_ios_folder_progress_callback progress_callback,
	void* user_context) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (!folder_path || !folder_path[0] || folder_path[0] != '/')
	{
		set_error("The game-folder path must be an absolute security-scoped path");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	if (const auto result = rpcs3::ios::validate_idle_operation_contract(
		g_lifecycle.state(), current_emulation_state()); result != RPCS3_IOS_OK)
	{
		set_error("Stop emulation before installing a game folder");
		return result;
	}
	if (const auto result = g_lifecycle.begin_content_install(); result != RPCS3_IOS_OK)
	{
		set_error("RPCS3Core must be ready before installing a game folder");
		return result;
	}

	try
	{
		auto progress = [progress_callback, user_context](u32 completed, u32 total, std::string_view stage)
		{
			if (!progress_callback)
			{
				return;
			}
			const std::string terminated{stage};
			progress_callback(user_context, completed, total, terminated.c_str());
		};

		emit_log(4, "Starting PlayStation 3 folder installation");
		const auto install_result = rpcs3::ios::install_game_folder(folder_path, progress);
		g_lifecycle.finish_content_install();
		if (install_result.error != rpcs3::ios::game_folder_install_error::none)
		{
			set_error(install_result.detail);
			emit_log(2, install_result.detail);
			return install_result.error == rpcs3::ios::game_folder_install_error::invalid_folder
				? RPCS3_IOS_FOLDER_INVALID
				: RPCS3_IOS_FOLDER_INSTALL_FAILED;
		}

		emit_log(4, fmt::format("Successfully installed PlayStation 3 folder: %s (%s)",
			install_result.title, install_result.title_id));
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception during game-folder installation");
	}

	g_lifecycle.finish_content_install();
	return RPCS3_IOS_FOLDER_INSTALL_FAILED;
}

extern "C" rpcs3_ios_status rpcs3_ios_install_game_patch(
	const char* expected_title_id,
	const char* package_path,
	rpcs3_ios_package_progress_callback progress_callback,
	void* user_context) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (!expected_title_id || !expected_title_id[0] ||
		!package_path || !package_path[0] || package_path[0] != '/')
	{
		set_error("Game-update installation requires a title ID and absolute sandbox package path");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	if (const auto result = rpcs3::ios::validate_idle_operation_contract(
		g_lifecycle.state(), current_emulation_state()); result != RPCS3_IOS_OK)
	{
		set_error("Stop emulation before installing a game update");
		return result;
	}
	if (const auto result = g_lifecycle.begin_content_install(); result != RPCS3_IOS_OK)
	{
		set_error("RPCS3Core must be ready before installing a game update");
		return result;
	}

	try
	{
		auto progress = [progress_callback, user_context](u32 completed, u32 total, std::string_view stage)
		{
			if (!progress_callback)
			{
				return;
			}
			const std::string terminated{stage};
			progress_callback(user_context, completed, total, terminated.c_str());
		};

		emit_log(4, fmt::format("Starting PlayStation 3 game-update installation for %s", expected_title_id));
		const auto install_result = rpcs3::ios::install_game_patch(
			expected_title_id, package_path, progress);
		g_lifecycle.finish_content_install();
		switch (install_result.error)
		{
		case rpcs3::ios::game_patch_install_error::none:
			emit_log(4, fmt::format("Successfully installed PlayStation 3 game update %s for %s",
				install_result.version, install_result.title_id));
			return RPCS3_IOS_OK;
		case rpcs3::ios::game_patch_install_error::invalid_patch:
			set_error(install_result.detail);
			emit_log(2, install_result.detail);
			return RPCS3_IOS_PATCH_INVALID;
		case rpcs3::ios::game_patch_install_error::title_mismatch:
			set_error(install_result.detail);
			emit_log(2, install_result.detail);
			return RPCS3_IOS_PATCH_TITLE_MISMATCH;
		case rpcs3::ios::game_patch_install_error::installation_failed:
			set_error(install_result.detail);
			emit_log(2, install_result.detail);
			return RPCS3_IOS_PATCH_INSTALL_FAILED;
		}
		set_error("Game-update installation returned an unknown result");
		return RPCS3_IOS_PATCH_INSTALL_FAILED;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception during game-update installation");
	}

	g_lifecycle.finish_content_install();
	return RPCS3_IOS_PATCH_INSTALL_FAILED;
}

extern "C" rpcs3_ios_status rpcs3_ios_enumerate_games(
	rpcs3_ios_game_callback callback,
	void* user_context) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (!callback)
	{
		set_error("Installed-game enumeration requires a callback");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	if (g_lifecycle.state() != RPCS3_IOS_STATE_READY)
	{
		set_error("RPCS3Core must be ready before enumerating installed games");
		return RPCS3_IOS_INVALID_STATE;
	}

	try
	{
		for (const auto& game : rpcs3::ios::installed_games())
		{
			const rpcs3_ios_game_info info{
				sizeof(rpcs3_ios_game_info),
				game.title_id.c_str(),
				game.title.c_str(),
				game.version.c_str(),
				game.category.c_str(),
				game.icon_path.c_str(),
			};
			callback(user_context, &info);
		}
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while enumerating installed games");
	}
	return RPCS3_IOS_INTERNAL_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_enumerate_game_patches(
	const char* title_id,
	rpcs3_ios_game_patch_callback callback,
	void* user_context) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (!title_id || !title_id[0] || !callback)
	{
		set_error("Game-update enumeration requires a title ID and callback");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	if (g_lifecycle.state() != RPCS3_IOS_STATE_READY)
	{
		set_error("RPCS3Core must be ready before enumerating installed game updates");
		return RPCS3_IOS_INVALID_STATE;
	}

	try
	{
		for (const auto& patch : rpcs3::ios::installed_game_patches(title_id))
		{
			const rpcs3_ios_game_patch_info info{
				sizeof(rpcs3_ios_game_patch_info),
				patch.title_id.c_str(),
				patch.title.c_str(),
				patch.version.c_str(),
			};
			callback(user_context, &info);
		}
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while enumerating installed game updates");
	}
	return RPCS3_IOS_INTERNAL_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_get_patch_repository_url(
	char* url,
	size_t url_capacity) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (!url || url_capacity == 0)
	{
		set_error("Patch-repository URL retrieval requires a writable output buffer");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	if (const auto result = rpcs3::ios::validate_idle_operation_contract(
		g_lifecycle.state(), current_emulation_state()); result != RPCS3_IOS_OK)
	{
		set_error("Stop emulation before managing game patches");
		return result;
	}

	try
	{
		const std::string repository_url = rpcs3::ios::patch_repository_url();
		if (repository_url.size() + 1 > url_capacity)
		{
			set_error("The patch-repository URL output buffer is too small");
			return RPCS3_IOS_INVALID_ARGUMENT;
		}
		std::memcpy(url, repository_url.c_str(), repository_url.size() + 1);
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while building the patch-repository URL");
	}
	return RPCS3_IOS_INTERNAL_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_install_patch_repository(
	const char* version,
	const char* sha256,
	const void* patch_content,
	size_t patch_content_size) noexcept
{
	std::lock_guard lock(g_api_mutex);
	constexpr size_t maximum_repository_size = 64u * 1024u * 1024u;
	if (!version || !version[0] || !sha256 || !sha256[0] || !patch_content ||
		patch_content_size == 0 || patch_content_size > maximum_repository_size)
	{
		set_error("Patch-repository installation requires versioned, checksummed YAML no larger than 64 MiB");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	if (const auto result = rpcs3::ios::validate_idle_operation_contract(
		g_lifecycle.state(), current_emulation_state()); result != RPCS3_IOS_OK)
	{
		set_error("Stop emulation before updating the game-patch repository");
		return result;
	}

	try
	{
		const auto result = rpcs3::ios::install_patch_repository(
			version,
			sha256,
			std::string_view{static_cast<const char*>(patch_content), patch_content_size});
		if (result.error == rpcs3::ios::patch_repository_install_error::none)
		{
			emit_log(4, "Installed the latest RPCS3 Patch Engine repository");
			return RPCS3_IOS_OK;
		}

		set_error(result.detail);
		emit_log(2, result.detail);
		return result.error == rpcs3::ios::patch_repository_install_error::invalid_repository
			? RPCS3_IOS_PATCH_REPOSITORY_INVALID
			: RPCS3_IOS_PATCH_REPOSITORY_INSTALL_FAILED;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while installing the patch repository");
	}
	return RPCS3_IOS_PATCH_REPOSITORY_INSTALL_FAILED;
}

extern "C" rpcs3_ios_status rpcs3_ios_enumerate_runtime_patches(
	const char* title_id,
	const char* app_version,
	rpcs3_ios_runtime_patch_callback callback,
	void* user_context) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (!title_id || !title_id[0] || !app_version || !callback)
	{
		set_error("Game-patch enumeration requires a title ID, application version, and callback");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	if (const auto result = rpcs3::ios::validate_idle_operation_contract(
		g_lifecycle.state(), current_emulation_state()); result != RPCS3_IOS_OK)
	{
		set_error("Stop emulation before managing game patches");
		return result;
	}

	try
	{
		const auto result = rpcs3::ios::runtime_patches_for_title(title_id, app_version);
		if (result.error != rpcs3::ios::runtime_patch_update_error::none)
		{
			set_error(result.detail);
			return RPCS3_IOS_PATCH_REPOSITORY_INVALID;
		}

		for (const auto& patch : result.patches)
		{
			const rpcs3_ios_runtime_patch_info info{
				sizeof(rpcs3_ios_runtime_patch_info),
				patch.enabled ? 1u : 0u,
				patch.configurable_count,
				0,
				patch.hash.c_str(),
				patch.title.c_str(),
				patch.description.c_str(),
				patch.patch_version.c_str(),
				patch.author.c_str(),
				patch.notes.c_str(),
				patch.patch_group.c_str(),
				patch.app_version.c_str(),
			};
			callback(user_context, &info);
		}
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while enumerating game patches");
	}
	return RPCS3_IOS_INTERNAL_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_set_runtime_patch_enabled(
	const char* title_id,
	const char* hash,
	const char* title,
	const char* app_version,
	const char* description,
	uint32_t enabled) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (!title_id || !title_id[0] || !hash || !hash[0] || !title || !title[0] ||
		!app_version || !app_version[0] || !description || !description[0] || enabled > 1)
	{
		set_error("Game-patch updates require the complete enumerated patch identity and a Boolean state");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	if (const auto result = rpcs3::ios::validate_idle_operation_contract(
		g_lifecycle.state(), current_emulation_state()); result != RPCS3_IOS_OK)
	{
		set_error("Stop emulation before changing game patches");
		return result;
	}

	try
	{
		const auto result = rpcs3::ios::set_runtime_patch_enabled(
			title_id, hash, title, app_version, description, enabled != 0);
		switch (result.error)
		{
		case rpcs3::ios::runtime_patch_update_error::none:
			emit_log(4, fmt::format("%s game patch '%s' for %s",
				enabled ? "Enabled" : "Disabled", description, title_id));
			return RPCS3_IOS_OK;
		case rpcs3::ios::runtime_patch_update_error::patch_not_found:
			set_error(result.detail);
			return RPCS3_IOS_RUNTIME_PATCH_NOT_FOUND;
		case rpcs3::ios::runtime_patch_update_error::storage_failed:
			set_error(result.detail);
			return RPCS3_IOS_RUNTIME_PATCH_SAVE_FAILED;
		case rpcs3::ios::runtime_patch_update_error::invalid_patch_files:
			set_error(result.detail);
			return RPCS3_IOS_PATCH_REPOSITORY_INVALID;
		}
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while changing a game patch");
	}
	return RPCS3_IOS_INTERNAL_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_enumerate_settings(
	rpcs3_ios_setting_callback setting_callback,
	rpcs3_ios_setting_option_callback option_callback,
	void* user_context) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (!setting_callback)
	{
		set_error("Settings enumeration requires a setting callback");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	if (const auto result = rpcs3::ios::validate_idle_operation_contract(
		g_lifecycle.state(), current_emulation_state()); result != RPCS3_IOS_OK)
	{
		set_error("Stop emulation before enumerating settings");
		return result;
	}

	try
	{
		settings_state_guard state_guard;
		bool has_custom_config = false;
		if (!load_settings_for_api({}, has_custom_config))
		{
			return RPCS3_IOS_SETTINGS_SAVE_FAILED;
		}
		enumerate_current_settings(
			setting_callback,
			option_callback,
			user_context,
			rpcs3::ios::setting_context::global);
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while enumerating settings");
	}
	return RPCS3_IOS_INTERNAL_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_set_setting(
	const char* key,
	const char* value) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (!key || !key[0] || !value)
	{
		set_error("A setting key and value are required");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	if (const auto result = rpcs3::ios::validate_idle_operation_contract(
		g_lifecycle.state(), current_emulation_state()); result != RPCS3_IOS_OK)
	{
		set_error("Stop emulation before changing settings");
		return result;
	}

	const auto* setting = rpcs3::ios::find_setting(key, rpcs3::ios::setting_context::global);
	if (!setting)
	{
		set_error(fmt::format("Unknown or unavailable iOS setting: %s", key));
		return RPCS3_IOS_SETTING_NOT_FOUND;
	}

	try
	{
		settings_state_guard state_guard;
		bool has_custom_config = false;
		if (!load_settings_for_api({}, has_custom_config))
		{
			return RPCS3_IOS_SETTINGS_SAVE_FAILED;
		}
		if (const auto result = update_current_setting(
			key, value, rpcs3::ios::setting_context::global); result != RPCS3_IOS_OK)
		{
			return result;
		}
		if (!rpcs3::ios::save_global_settings())
		{
			set_error("Unable to atomically save RPCS3 config.yml");
			return RPCS3_IOS_SETTINGS_SAVE_FAILED;
		}
		emit_log(4, fmt::format("Saved setting %s = %s", key, value));
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while changing a setting");
	}
	return RPCS3_IOS_INTERNAL_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_reset_settings(void) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (const auto result = rpcs3::ios::validate_idle_operation_contract(
		g_lifecycle.state(), current_emulation_state()); result != RPCS3_IOS_OK)
	{
		set_error("Stop emulation before resetting settings");
		return result;
	}

	try
	{
		settings_state_guard state_guard;
		bool has_custom_config = false;
		if (!load_settings_for_api({}, has_custom_config))
		{
			return RPCS3_IOS_SETTINGS_SAVE_FAILED;
		}
		reset_current_settings(rpcs3::ios::setting_context::global);
		if (!rpcs3::ios::save_global_settings())
		{
			set_error("Unable to atomically save default RPCS3 settings");
			return RPCS3_IOS_SETTINGS_SAVE_FAILED;
		}
		emit_log(4, "Restored all iOS-exposed RPCS3 settings to defaults");
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while resetting settings");
	}
	return RPCS3_IOS_INTERNAL_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_enumerate_game_settings(
	const char* title_id,
	rpcs3_ios_setting_callback setting_callback,
	rpcs3_ios_setting_option_callback option_callback,
	void* user_context,
	uint32_t* has_custom_config) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (!setting_callback || !has_custom_config)
	{
		set_error("Game-settings enumeration requires setting and custom-state outputs");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	*has_custom_config = 0;
	if (const auto result = rpcs3::ios::validate_idle_operation_contract(
		g_lifecycle.state(), current_emulation_state()); result != RPCS3_IOS_OK)
	{
		set_error("Stop emulation before enumerating game settings");
		return result;
	}
	rpcs3::ios::installed_game game;
	if (const auto result = validate_game_settings_target(title_id, &game); result != RPCS3_IOS_OK)
	{
		return result;
	}

	try
	{
		settings_state_guard state_guard;
		bool custom_config = false;
		const bool loaded = load_settings_for_api(title_id, custom_config);
		*has_custom_config = custom_config ? 1u : 0u;
		if (!loaded)
		{
			return RPCS3_IOS_SETTINGS_SAVE_FAILED;
		}
		normalize_current_game_resolution(game.resolution);
		enumerate_current_settings(
			setting_callback,
			option_callback,
			user_context,
			rpcs3::ios::setting_context::game,
			game.resolution);
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while enumerating game settings");
	}
	return RPCS3_IOS_INTERNAL_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_set_game_setting(
	const char* title_id,
	const char* key,
	const char* value) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (!key || !key[0] || !value)
	{
		set_error("A setting key and value are required");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	if (const auto result = rpcs3::ios::validate_idle_operation_contract(
		g_lifecycle.state(), current_emulation_state()); result != RPCS3_IOS_OK)
	{
		set_error("Stop emulation before changing game settings");
		return result;
	}
	rpcs3::ios::installed_game game;
	if (const auto result = validate_game_settings_target(title_id, &game); result != RPCS3_IOS_OK)
	{
		return result;
	}

	try
	{
		settings_state_guard state_guard;
		bool has_custom_config = false;
		if (!load_settings_for_api(title_id, has_custom_config))
		{
			return RPCS3_IOS_SETTINGS_SAVE_FAILED;
		}
		normalize_current_game_resolution(game.resolution);
		if (std::string_view{key} == "gpu.resolution")
		{
			const std::vector<std::string> options = supported_game_resolution_options(game.resolution);
			if (std::ranges::find(options, value) == options.end())
			{
				set_error(fmt::format("The game does not support Default Resolution '%s'", value));
				return RPCS3_IOS_SETTING_INVALID;
			}
		}
		if (const auto result = update_current_setting(
			key, value, rpcs3::ios::setting_context::game); result != RPCS3_IOS_OK)
		{
			return result;
		}
		if (!rpcs3::ios::save_game_settings(title_id))
		{
			set_error(fmt::format("Unable to save the custom configuration for %s", title_id));
			return RPCS3_IOS_SETTINGS_SAVE_FAILED;
		}
		emit_log(4, fmt::format("Saved game setting %s = %s for %s", key, value, title_id));
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while changing a game setting");
	}
	return RPCS3_IOS_INTERNAL_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_reset_game_settings(const char* title_id) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (const auto result = rpcs3::ios::validate_idle_operation_contract(
		g_lifecycle.state(), current_emulation_state()); result != RPCS3_IOS_OK)
	{
		set_error("Stop emulation before resetting game settings");
		return result;
	}
	if (const auto result = validate_game_settings_target(title_id); result != RPCS3_IOS_OK)
	{
		return result;
	}

	try
	{
		settings_state_guard state_guard;
		bool has_custom_config = false;
		if (!load_settings_for_api(title_id, has_custom_config))
		{
			return RPCS3_IOS_SETTINGS_SAVE_FAILED;
		}
		reset_current_settings(rpcs3::ios::setting_context::game);
		if (!rpcs3::ios::save_game_settings(title_id))
		{
			set_error(fmt::format("Unable to save default game settings for %s", title_id));
			return RPCS3_IOS_SETTINGS_SAVE_FAILED;
		}
		emit_log(4, fmt::format("Restored all iOS-exposed settings to RPCS3 defaults for %s", title_id));
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while resetting game settings");
	}
	return RPCS3_IOS_INTERNAL_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_remove_game_settings(const char* title_id) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (const auto result = rpcs3::ios::validate_idle_operation_contract(
		g_lifecycle.state(), current_emulation_state()); result != RPCS3_IOS_OK)
	{
		set_error("Stop emulation before removing game settings");
		return result;
	}
	if (const auto result = validate_game_settings_target(title_id); result != RPCS3_IOS_OK)
	{
		return result;
	}

	try
	{
		if (!rpcs3::ios::remove_game_settings(title_id))
		{
			set_error(fmt::format("Unable to remove the custom configuration for %s", title_id));
			return RPCS3_IOS_SETTINGS_SAVE_FAILED;
		}
		emit_log(4, fmt::format("Removed the custom configuration for %s; the game now uses global settings", title_id));
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while removing game settings");
	}
	return RPCS3_IOS_INTERNAL_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_get_rpcn_config(
	rpcs3_ios_rpcn_config_callback callback,
	void* user_context) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (!callback)
	{
		set_error("RPCN configuration retrieval requires a callback");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	if (const auto result = validate_rpcn_operation(false); result != RPCS3_IOS_OK)
	{
		return result;
	}

	try
	{
		const std::string username = g_cfg_rpcn.get_npid();
		const std::string host = g_cfg_rpcn.get_host();
		const std::string password = g_cfg_rpcn.get_password();
		const std::string token = g_cfg_rpcn.get_token();
		const std::string online_name = g_rpcn_client && g_rpcn_client->is_authentified()
			? g_rpcn_client->get_online_name() : std::string{};
		const std::string avatar_url = g_rpcn_client && g_rpcn_client->is_authentified()
			? g_rpcn_client->get_avatar_url() : std::string{};
		const rpcs3_ios_rpcn_config_info info{
			sizeof(rpcs3_ios_rpcn_config_info),
			password.empty() ? 0u : 1u,
			token.empty() ? 0u : 1u,
			g_cfg_rpcn.get_ipv6_support() ? 1u : 0u,
			g_rpcn_client && g_rpcn_client->is_connected() ? 1u : 0u,
			g_rpcn_client && g_rpcn_client->is_authentified() ? 1u : 0u,
			username.c_str(),
			host.c_str(),
			online_name.c_str(),
			avatar_url.c_str(),
		};
		callback(user_context, &info);
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while reading RPCN configuration");
	}
	return RPCS3_IOS_INTERNAL_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_enumerate_rpcn_servers(
	rpcs3_ios_rpcn_server_callback callback,
	void* user_context) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (!callback)
	{
		set_error("RPCN server enumeration requires a callback");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	if (const auto result = validate_rpcn_operation(false); result != RPCS3_IOS_OK)
	{
		return result;
	}

	try
	{
		const std::string selected_host = g_cfg_rpcn.get_host();
		for (const auto& [description, host] : g_cfg_rpcn.get_hosts())
		{
			const bool official = description == "Official RPCN Server" && host == "np.rpcs3.net";
			const rpcs3_ios_rpcn_server_info info{
				sizeof(rpcs3_ios_rpcn_server_info),
				host == selected_host ? 1u : 0u,
				official ? 0u : 1u,
				0,
				description.c_str(),
				host.c_str(),
			};
			callback(user_context, &info);
		}
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while enumerating RPCN servers");
	}
	return RPCS3_IOS_INTERNAL_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_set_rpcn_server(const char* host) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (!host || !host[0])
	{
		set_error("An RPCN server host is required");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	if (const auto result = validate_rpcn_operation(true); result != RPCS3_IOS_OK)
	{
		return result;
	}

	try
	{
		const auto hosts = g_cfg_rpcn.get_hosts();
		if (std::ranges::none_of(hosts, [host](const auto& item) { return item.second == host; }))
		{
			set_error("The selected RPCN server is not in the configured server list");
			return RPCS3_IOS_RPCN_SERVER_NOT_FOUND;
		}
		g_cfg_rpcn.set_host(host);
		if (!g_cfg_rpcn.save())
		{
			set_error("Unable to save the selected RPCN server");
			return RPCS3_IOS_SETTINGS_SAVE_FAILED;
		}
		if (g_rpcn_client)
		{
			g_rpcn_client->reconnect();
		}
		emit_log(4, fmt::format("Selected RPCN server %s", host));
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while selecting an RPCN server");
	}
	return RPCS3_IOS_RPCN_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_add_rpcn_server(
	const char* description,
	const char* host) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (!description || !description[0] || !host || !host[0])
	{
		set_error("RPCN server description and host are required");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	if (const auto result = validate_rpcn_operation(true); result != RPCS3_IOS_OK)
	{
		return result;
	}

	try
	{
		const std::string_view description_view{description};
		if (description_view.size() > 80 || description_view.find('|') != std::string_view::npos || !parse_rpcn_host(host))
		{
			set_error("The RPCN server description or host is invalid");
			return RPCS3_IOS_INVALID_ARGUMENT;
		}
		if (!g_cfg_rpcn.add_host(description, host))
		{
			set_error("That RPCN server already exists");
			return RPCS3_IOS_RPCN_SERVER_EXISTS;
		}
		if (!g_cfg_rpcn.save())
		{
			set_error("Unable to save the RPCN server list");
			return RPCS3_IOS_SETTINGS_SAVE_FAILED;
		}
		emit_log(4, fmt::format("Added RPCN server %s (%s)", description, host));
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while adding an RPCN server");
	}
	return RPCS3_IOS_RPCN_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_remove_rpcn_server(
	const char* description,
	const char* host) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (!description || !description[0] || !host || !host[0])
	{
		set_error("RPCN server description and host are required");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	if (const auto result = validate_rpcn_operation(true); result != RPCS3_IOS_OK)
	{
		return result;
	}
	if (std::string_view{description} == "Official RPCN Server" && std::string_view{host} == "np.rpcs3.net")
	{
		set_error("The official RPCN server cannot be removed");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}

	try
	{
		if (!g_cfg_rpcn.del_host(description, host))
		{
			set_error("The RPCN server was not found");
			return RPCS3_IOS_RPCN_SERVER_NOT_FOUND;
		}
		if (g_cfg_rpcn.get_host() == host)
		{
			g_cfg_rpcn.set_host("np.rpcs3.net");
		}
		if (!g_cfg_rpcn.save())
		{
			set_error("Unable to save the RPCN server list");
			return RPCS3_IOS_SETTINGS_SAVE_FAILED;
		}
		if (g_rpcn_client)
		{
			g_rpcn_client->reconnect();
		}
		emit_log(4, fmt::format("Removed RPCN server %s (%s)", description, host));
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while removing an RPCN server");
	}
	return RPCS3_IOS_RPCN_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_set_rpcn_credentials(
	const char* username,
	const char* password,
	const char* token,
	uint32_t ipv6_support) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (!username || !password || !token || ipv6_support > 1 ||
		!valid_rpcn_username(username) || !valid_rpcn_token(token))
	{
		set_error("RPCN credentials require a valid username, password, token, and IPv6 state");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	if (const auto result = validate_rpcn_operation(true); result != RPCS3_IOS_OK)
	{
		return result;
	}

	try
	{
		auto derived = derive_rpcn_password(password);
		if (!derived)
		{
			set_error("The RPCN password is empty, too long, or could not be transformed");
			return RPCS3_IOS_INVALID_ARGUMENT;
		}
		if (!persist_rpcn_profile(username, std::move(*derived), token, ipv6_support != 0))
		{
			set_error("Unable to save the public RPCN profile");
			return RPCS3_IOS_SETTINGS_SAVE_FAILED;
		}
		if (g_rpcn_client)
		{
			g_rpcn_client->reconnect();
		}
		emit_log(4, fmt::format("Saved RPCN credentials for %s; the password remains in memory only", username));
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while saving RPCN credentials");
	}
	return RPCS3_IOS_RPCN_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_create_rpcn_account(
	const char* username,
	const char* password,
	const char* email) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (!username || !password || !email || !valid_rpcn_username(username) || !valid_rpcn_email(email))
	{
		set_error("RPCN account creation requires a valid username, password, and email address");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	if (const auto result = validate_rpcn_operation(true); result != RPCS3_IOS_OK)
	{
		return result;
	}

	try
	{
		auto derived = derive_rpcn_password(password);
		if (!derived)
		{
			set_error("The RPCN password is empty, too long, or could not be transformed");
			return RPCS3_IOS_INVALID_ARGUMENT;
		}
		if (const auto result = connect_rpcn(false, true); result != RPCS3_IOS_OK)
		{
			return result;
		}
		constexpr std::string_view avatar = "https://rpcs3.net/cdn/netplay/DefaultAvatar.png";
		const auto error = g_rpcn_client->create_user(username, *derived, username, avatar, email);
		if (error != rpcn::ErrorType::NoError)
		{
			set_error(rpcn_error_detail(error));
			return RPCS3_IOS_RPCN_ERROR;
		}
		if (!persist_rpcn_profile(username, std::move(*derived), {}, g_cfg_rpcn.get_ipv6_support()))
		{
			set_error("The RPCN account was created, but its public profile could not be saved");
			return RPCS3_IOS_SETTINGS_SAVE_FAILED;
		}
		g_rpcn_client->reconnect();
		emit_log(4, fmt::format("Created RPCN account %s", username));
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while creating an RPCN account");
	}
	return RPCS3_IOS_RPCN_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_test_rpcn_account(void) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (const auto result = validate_rpcn_operation(false); result != RPCS3_IOS_OK)
	{
		return result;
	}
	try
	{
		if (const auto result = connect_rpcn(true, false); result != RPCS3_IOS_OK)
		{
			return result;
		}
		emit_log(4, fmt::format("Authenticated with RPCN as %s", g_cfg_rpcn.get_npid()));
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while testing the RPCN account");
	}
	return RPCS3_IOS_RPCN_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_resend_rpcn_token(void) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (const auto result = validate_rpcn_operation(true); result != RPCS3_IOS_OK)
	{
		return result;
	}
	if (g_cfg_rpcn.get_npid().empty() || g_cfg_rpcn.get_password().empty())
	{
		set_error("Configure an RPCN username and password first");
		return RPCS3_IOS_RPCN_NOT_CONFIGURED;
	}
	try
	{
		if (const auto result = connect_rpcn(false, true); result != RPCS3_IOS_OK)
		{
			return result;
		}
		const auto error = g_rpcn_client->resend_token(g_cfg_rpcn.get_npid(), g_cfg_rpcn.get_password());
		if (error != rpcn::ErrorType::NoError)
		{
			set_error(rpcn_error_detail(error));
			return RPCS3_IOS_RPCN_ERROR;
		}
		emit_log(4, "Requested a new RPCN verification token");
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while resending the RPCN token");
	}
	return RPCS3_IOS_RPCN_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_request_rpcn_password_reset(
	const char* username,
	const char* email) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (!username || !email || !valid_rpcn_username(username) || !valid_rpcn_email(email))
	{
		set_error("RPCN password reset requires a valid username and email address");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	if (const auto result = validate_rpcn_operation(true); result != RPCS3_IOS_OK)
	{
		return result;
	}
	try
	{
		if (const auto result = connect_rpcn(false, true); result != RPCS3_IOS_OK)
		{
			return result;
		}
		const auto error = g_rpcn_client->send_reset_token(username, email);
		if (error != rpcn::ErrorType::NoError)
		{
			set_error(rpcn_error_detail(error));
			return RPCS3_IOS_RPCN_ERROR;
		}
		emit_log(4, fmt::format("Requested an RPCN password-reset token for %s", username));
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while requesting an RPCN password reset");
	}
	return RPCS3_IOS_RPCN_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_reset_rpcn_password(
	const char* username,
	const char* reset_token,
	const char* new_password) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (!username || !reset_token || !new_password || !valid_rpcn_username(username) ||
		!valid_rpcn_token(reset_token) || !reset_token[0])
	{
		set_error("RPCN password reset requires a valid username, 16-character token, and new password");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	if (const auto result = validate_rpcn_operation(true); result != RPCS3_IOS_OK)
	{
		return result;
	}
	try
	{
		auto derived = derive_rpcn_password(new_password);
		if (!derived)
		{
			set_error("The new RPCN password is empty, too long, or could not be transformed");
			return RPCS3_IOS_INVALID_ARGUMENT;
		}
		if (const auto result = connect_rpcn(false, true); result != RPCS3_IOS_OK)
		{
			return result;
		}
		const auto error = g_rpcn_client->reset_password(username, reset_token, *derived);
		if (error != rpcn::ErrorType::NoError)
		{
			set_error(rpcn_error_detail(error));
			return RPCS3_IOS_RPCN_ERROR;
		}
		if (g_cfg_rpcn.get_npid() == username &&
			!persist_rpcn_profile(username, std::move(*derived), g_cfg_rpcn.get_token(), g_cfg_rpcn.get_ipv6_support()))
		{
			set_error("The RPCN password changed, but the local public profile could not be saved");
			return RPCS3_IOS_SETTINGS_SAVE_FAILED;
		}
		g_rpcn_client->reconnect();
		emit_log(4, fmt::format("Reset the RPCN password for %s", username));
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while resetting the RPCN password");
	}
	return RPCS3_IOS_RPCN_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_delete_rpcn_account(void) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (const auto result = validate_rpcn_operation(true); result != RPCS3_IOS_OK)
	{
		return result;
	}
	if (g_cfg_rpcn.get_npid().empty() || g_cfg_rpcn.get_password().empty())
	{
		set_error("Configure an RPCN username and password first");
		return RPCS3_IOS_RPCN_NOT_CONFIGURED;
	}
	try
	{
		if (const auto result = connect_rpcn(false, true); result != RPCS3_IOS_OK)
		{
			return result;
		}
		const auto error = g_rpcn_client->delete_account();
		if (error != rpcn::ErrorType::NoError)
		{
			set_error(rpcn_error_detail(error));
			return RPCS3_IOS_RPCN_ERROR;
		}
		g_cfg_rpcn.clear_runtime_credentials();
		g_cfg_rpcn.set_npid({});
		g_cfg_rpcn.set_password({});
		g_cfg_rpcn.set_token({});
		if (!g_cfg_rpcn.save())
		{
			set_error("The RPCN account was deleted, but the local profile could not be cleared");
			return RPCS3_IOS_SETTINGS_SAVE_FAILED;
		}
		g_rpcn_client.reset();
		emit_log(4, "Deleted the RPCN account and cleared its local profile");
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while deleting the RPCN account");
	}
	return RPCS3_IOS_RPCN_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_enumerate_rpcn_social(
	rpcs3_ios_rpcn_social_callback callback,
	void* user_context) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (!callback)
	{
		set_error("RPCN social enumeration requires a callback");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	if (const auto result = validate_rpcn_operation(false); result != RPCS3_IOS_OK)
	{
		return result;
	}
	try
	{
		if (const auto result = connect_rpcn(true, false); result != RPCS3_IOS_OK)
		{
			return result;
		}

		rpcn::friend_data data;
		g_rpcn_client->get_friends(data);
		const auto emit_entry = [callback, user_context](uint32_t kind, std::string_view username,
			bool online = false, u64 timestamp = 0, std::string_view presence_title = {},
			std::string_view presence_status = {}, std::string_view presence_comment = {},
			std::string_view history_description = {})
		{
			const std::string username_copy{username};
			const std::string title_copy{presence_title};
			const std::string status_copy{presence_status};
			const std::string comment_copy{presence_comment};
			const std::string description_copy{history_description};
			const rpcs3_ios_rpcn_social_info info{
				sizeof(rpcs3_ios_rpcn_social_info), kind, online ? 1u : 0u, 0, timestamp,
				username_copy.c_str(), title_copy.c_str(), status_copy.c_str(), comment_copy.c_str(), description_copy.c_str(),
			};
			callback(user_context, &info);
		};

		for (const auto& [username, presence] : data.friends)
		{
			emit_entry(RPCS3_IOS_RPCN_SOCIAL_FRIEND, username, presence.online, presence.timestamp,
				presence.pr_title, presence.pr_status, presence.pr_comment);
		}
		for (const auto& username : data.requests_received)
		{
			emit_entry(RPCS3_IOS_RPCN_SOCIAL_REQUEST_RECEIVED, username);
		}
		for (const auto& username : data.requests_sent)
		{
			emit_entry(RPCS3_IOS_RPCN_SOCIAL_REQUEST_SENT, username);
		}
		for (const auto& username : data.blocked)
		{
			emit_entry(RPCS3_IOS_RPCN_SOCIAL_BLOCKED, username);
		}
		for (const auto& [username, history] : np::load_players_history())
		{
			if (!data.friends.contains(username) && !data.requests_received.contains(username) &&
				!data.requests_sent.contains(username) && !data.blocked.contains(username))
			{
				emit_entry(RPCS3_IOS_RPCN_SOCIAL_RECENT_PLAYER, username, false,
					history.timestamp, {}, {}, {}, history.description);
			}
		}
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while enumerating RPCN social data");
	}
	return RPCS3_IOS_RPCN_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_perform_rpcn_social_action(
	uint32_t action,
	const char* username) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (!username || !valid_rpcn_username(username) || action > RPCS3_IOS_RPCN_SOCIAL_UNBLOCK_USER)
	{
		set_error("RPCN social actions require a valid action and username");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	if (const auto result = validate_rpcn_operation(false); result != RPCS3_IOS_OK)
	{
		return result;
	}
	try
	{
		if (const auto result = connect_rpcn(true, false); result != RPCS3_IOS_OK)
		{
			return result;
		}

		switch (action)
		{
		case RPCS3_IOS_RPCN_SOCIAL_ADD_FRIEND:
		case RPCS3_IOS_RPCN_SOCIAL_ACCEPT_REQUEST:
		{
			const auto error = g_rpcn_client->add_friend(username);
			if (!error)
			{
				set_error("RPCN did not return a friend-request result");
				return RPCS3_IOS_RPCN_ERROR;
			}
			if (*error != rpcn::ErrorType::NoError)
			{
				set_error(rpcn_error_detail(*error));
				return RPCS3_IOS_RPCN_ERROR;
			}
			break;
		}
		case RPCS3_IOS_RPCN_SOCIAL_REMOVE_FRIEND:
		case RPCS3_IOS_RPCN_SOCIAL_REJECT_REQUEST:
		case RPCS3_IOS_RPCN_SOCIAL_CANCEL_REQUEST:
			if (!g_rpcn_client->remove_friend(username))
			{
				set_error("RPCN could not remove the friend or request");
				return RPCS3_IOS_RPCN_ERROR;
			}
			break;
		case RPCS3_IOS_RPCN_SOCIAL_BLOCK_USER:
		{
			const auto error = g_rpcn_client->add_block(username);
			if (!error)
			{
				set_error("RPCN did not return a block result");
				return RPCS3_IOS_RPCN_ERROR;
			}
			if (*error != rpcn::ErrorType::NoError)
			{
				set_error(rpcn_error_detail(*error));
				return RPCS3_IOS_RPCN_ERROR;
			}
			break;
		}
		case RPCS3_IOS_RPCN_SOCIAL_UNBLOCK_USER:
			if (!g_rpcn_client->remove_block(username))
			{
				set_error("RPCN could not unblock the user");
				return RPCS3_IOS_RPCN_ERROR;
			}
			break;
		default:
			set_error("Unknown RPCN social action");
			return RPCS3_IOS_INVALID_ARGUMENT;
		}

		emit_log(4, fmt::format("Completed RPCN social action %u for %s", action, username));
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while performing an RPCN social action");
	}
	return RPCS3_IOS_RPCN_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_set_display_surface(
	const rpcs3_ios_display_surface* surface) noexcept
{
	// This operation deliberately does not take g_api_mutex: boot owns that
	// mutex while RPCS3 compiles, but the existing layer must still resize.
	if (!g_accept_display_surfaces)
	{
		set_error("RPCS3Core must be ready before updating the display surface");
		return RPCS3_IOS_INVALID_STATE;
	}

	const bool emulation_stopped =
		Emu.GetStatus(false) == system_state::stopped;
	const rpcs3_ios_status result =
		g_display_surface.update(surface, emulation_stopped);
	if (result == RPCS3_IOS_INVALID_ARGUMENT)
	{
		set_error("The iOS display surface contract is invalid");
	}
	else if (result == RPCS3_IOS_INVALID_STATE)
	{
		set_error("Stop emulation before replacing or detaching the iOS display surface");
	}
	return result;
}

extern "C" rpcs3_ios_status rpcs3_ios_set_pad_state(
	uint32_t player_index,
	const rpcs3_ios_pad_state* state) noexcept
{
	// Boot owns g_api_mutex during lengthy firmware compilation. Input must
	// remain independently writable so controller events never wait for boot.
	if (!g_accept_pad_state)
	{
		set_error("RPCS3Core must be ready before updating iOS pad input");
		return RPCS3_IOS_INVALID_STATE;
	}

	const auto result = rpcs3::ios::shared_pad_states().update(player_index, state);
	if (result != RPCS3_IOS_OK)
	{
		set_error("The iOS pad-state contract is invalid");
	}
	return result;
}

extern "C" rpcs3_ios_status rpcs3_ios_get_pad_feedback(
	uint32_t player_index,
	rpcs3_ios_pad_feedback* feedback) noexcept
{
	if (!rpcs3::ios::validate_pad_player_index(player_index) || !feedback ||
		feedback->struct_size < sizeof(rpcs3_ios_pad_feedback))
	{
		set_error("Pad feedback requires a compatible output structure");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}

	*feedback = rpcs3::ios::shared_pad_feedback().snapshot(player_index);
	return RPCS3_IOS_OK;
}

extern "C" rpcs3_ios_status rpcs3_ios_boot_vsh(void) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (const auto result = rpcs3::ios::validate_idle_operation_contract(
		g_lifecycle.state(), current_emulation_state()); result != RPCS3_IOS_OK)
	{
		set_error("RPCS3Core must be ready and emulation stopped before booting XMB");
		return result;
	}
	if (!g_display_surface.snapshot().valid())
	{
		set_error("Attach a valid iOS Metal display surface before booting XMB");
		return RPCS3_IOS_INVALID_STATE;
	}

	try
	{
		const std::string vsh_path = g_cfg_vfs.get_dev_flash() + "vsh/module/vsh.self";
		if (!fs::is_file(vsh_path))
		{
			set_error("PlayStation 3 firmware is missing vsh/module/vsh.self");
			return RPCS3_IOS_BOOT_FAILED;
		}

		emit_log(4, "Booting the PlayStation 3 XMB from installed firmware");
		prepare_rpcn_for_guest_boot();
		Emu.SetForceBoot(true);
		const game_boot_result result = Emu.BootGame(vsh_path);
		if (result != game_boot_result::no_errors)
		{
			Emu.SetForceBoot(false);
			set_error(fmt::format("XMB boot failed: %s", result));
			return RPCS3_IOS_BOOT_FAILED;
		}

		emit_log(4, "PlayStation 3 XMB boot request completed");
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while booting the PlayStation 3 XMB");
	}

	Emu.SetForceBoot(false);
	return RPCS3_IOS_BOOT_FAILED;
}

extern "C" rpcs3_ios_status rpcs3_ios_boot_game(const char* title_id) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (!title_id || !title_id[0])
	{
		set_error("An installed title ID is required");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	const std::string requested_title_id{title_id};
	if (requested_title_id.size() > 32 ||
		!std::all_of(requested_title_id.begin(), requested_title_id.end(), [](unsigned char character)
		{
			return std::isalnum(character) || character == '_' || character == '-';
		}))
	{
		set_error("The installed title ID contains invalid characters");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}
	if (const auto result = rpcs3::ios::validate_idle_operation_contract(
		g_lifecycle.state(), current_emulation_state()); result != RPCS3_IOS_OK)
	{
		set_error("RPCS3Core must be ready and emulation stopped before booting a game");
		return result;
	}
	if (!g_display_surface.snapshot().valid())
	{
		set_error("Attach a valid iOS Metal display surface before booting a game");
		return RPCS3_IOS_INVALID_STATE;
	}

	try
	{
		const auto game = rpcs3::ios::find_installed_game(requested_title_id);
		if (!game)
		{
			set_error(fmt::format("Installed game not found: %s", requested_title_id));
			return RPCS3_IOS_GAME_NOT_FOUND;
		}

		emit_log(4, fmt::format("Booting installed game %s (%s)", game->title, game->title_id));
		prepare_rpcn_for_guest_boot();
		Emu.SetForceBoot(true);
		const game_boot_result result = Emu.BootGame(game->path, game->title_id);
		if (result != game_boot_result::no_errors)
		{
			Emu.SetForceBoot(false);
			set_error(fmt::format("Game boot failed: %s", result));
			return RPCS3_IOS_BOOT_FAILED;
		}

		emit_log(4, fmt::format("Installed game boot request completed: %s", game->title_id));
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while booting the installed game");
	}

	Emu.SetForceBoot(false);
	return RPCS3_IOS_BOOT_FAILED;
}

extern "C" rpcs3_ios_emulation_state rpcs3_ios_get_emulation_state(void) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (g_lifecycle.state() != RPCS3_IOS_STATE_READY)
	{
		return RPCS3_IOS_EMULATION_STATE_UNKNOWN;
	}
	return current_emulation_state();
}

extern "C" rpcs3_ios_status rpcs3_ios_get_boot_progress(
	uint32_t* completed,
	uint32_t* total,
	char* stage,
	size_t stage_capacity) noexcept
{
	std::lock_guard lock(g_progress_mutex);
	if (!completed || !total || !stage || stage_capacity == 0)
	{
		set_error("Boot progress requires output counters and a non-empty stage buffer");
		return RPCS3_IOS_INVALID_ARGUMENT;
	}

	*completed = 0;
	*total = 0;
	stage[0] = '\0';

	try
	{
		const boot_progress_snapshot snapshot = capture_boot_progress();
		const bool has_counters = snapshot.files_total || snapshot.files_done ||
			snapshot.modules_total || snapshot.modules_done;
		std::string detail = snapshot.text;
		if (detail.empty() && has_counters)
		{
			detail = "Preparing title modules";
		}

		if (snapshot.files_total)
		{
			fmt::append(detail, "%sFiles %u of %u", detail.empty() ? "" : " | ",
				snapshot.files_done, snapshot.files_total);
		}
		if (snapshot.modules_total)
		{
			fmt::append(detail, "%sModules %u of %u", detail.empty() ? "" : " | ",
				snapshot.modules_done, snapshot.modules_total);

			const bool use_bits = snapshot.file_bits_known && snapshot.file_bits_total;
			const u64 known_files = use_bits ? snapshot.file_bits_known : snapshot.files_total;
			const u64 total_units = utils::rational_mul<u64>(
				std::max<u64>(snapshot.modules_total, 1),
				std::max<u64>(use_bits ? snapshot.file_bits_total : snapshot.files_total, 1),
				std::max<u64>(known_files, 1));
			*total = 1000;
			*completed = static_cast<u32>(snapshot.modules_done >= total_units
				? 1000
				: utils::rational_mul<u64>(snapshot.modules_done, 1000, total_units));
		}

		const size_t copy_size = std::min(detail.size(), stage_capacity - 1);
		std::memcpy(stage, detail.data(), copy_size);
		stage[copy_size] = '\0';
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while reading boot progress");
	}

	return RPCS3_IOS_INTERNAL_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_get_performance_metrics(
	rpcs3_ios_performance_metrics* metrics) noexcept
{
	try
	{
		const rpcs3_ios_status result = rpcs3::ios::capture_performance_metrics(metrics);
		if (result != RPCS3_IOS_OK)
		{
			set_error("Performance metrics require a compatible output structure");
		}
		return result;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while sampling performance metrics");
	}

	return RPCS3_IOS_INTERNAL_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_pause_emulation(void) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (const auto result = rpcs3::ios::validate_pause_operation_contract(
		g_lifecycle.state(), current_emulation_state()); result != RPCS3_IOS_OK)
	{
		set_error("RPCS3Core must be ready and emulation running before pausing");
		return result;
	}

	try
	{
		emit_log(4, "Pausing the current PlayStation 3 emulation session");
		if (!Emu.Pause(false, false) || Emu.GetStatus(false) != system_state::paused)
		{
			set_error("RPCS3 did not enter the paused state");
			return RPCS3_IOS_INVALID_STATE;
		}

		rpcs3::ios::shared_pad_feedback().clear();
		emit_log(4, "PlayStation 3 emulation paused");
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while pausing PlayStation 3 emulation");
	}

	return RPCS3_IOS_INTERNAL_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_resume_emulation(void) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (const auto result = rpcs3::ios::validate_resume_operation_contract(
		g_lifecycle.state(), current_emulation_state()); result != RPCS3_IOS_OK ||
		Emu.GetStatus(false) != system_state::paused)
	{
		set_error("RPCS3Core must be ready and emulation paused before resuming");
		return result == RPCS3_IOS_OK ? RPCS3_IOS_INVALID_STATE : result;
	}

	try
	{
		emit_log(4, "Resuming the current PlayStation 3 emulation session");
		Emu.Resume();
		if (Emu.GetStatus(false) != system_state::running)
		{
			set_error("RPCS3 did not return to the running state");
			return RPCS3_IOS_INVALID_STATE;
		}

		emit_log(4, "PlayStation 3 emulation resumed");
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while resuming PlayStation 3 emulation");
	}

	return RPCS3_IOS_INTERNAL_ERROR;
}

extern "C" rpcs3_ios_status rpcs3_ios_stop_emulation(void) noexcept
{
	std::lock_guard lock(g_api_mutex);
	if (g_lifecycle.state() != RPCS3_IOS_STATE_READY)
	{
		set_error("RPCS3Core must be ready before stopping emulation");
		return RPCS3_IOS_INVALID_STATE;
	}

	try
	{
		if (Emu.GetStatus(false) == system_state::stopped)
		{
			return RPCS3_IOS_OK;
		}

		emit_log(4, "Stopping the current PlayStation 3 emulation session");
		Emu.GracefulShutdown(false, false);
		if (!wait_for_emulation_stop())
		{
			set_error("RPCS3 did not reach the stopped state");
			return RPCS3_IOS_STOP_FAILED;
		}

		rpcs3::ios::shared_pad_feedback().clear();
		emit_log(4, "PlayStation 3 emulation stopped; RPCS3Core remains initialized");
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception while stopping PlayStation 3 emulation");
	}

	return RPCS3_IOS_STOP_FAILED;
}

extern "C" rpcs3_ios_state rpcs3_ios_get_state(void) noexcept
{
	std::lock_guard lock(g_api_mutex);
	return g_lifecycle.state();
}

extern "C" rpcs3_ios_status rpcs3_ios_shutdown(void) noexcept
{
	std::lock_guard lock(g_api_mutex);
	bool should_run = false;
	if (const auto result = g_lifecycle.begin_shutdown(should_run); result != RPCS3_IOS_OK)
	{
		set_error("RPCS3Core is busy");
		return result;
	}
	if (!should_run)
	{
		return RPCS3_IOS_OK;
	}
	g_accept_display_surfaces = false;
	g_accept_pad_state = false;
	rpcs3::ios::shared_pad_states().clear();
	rpcs3::ios::shared_pad_feedback().clear();
	try
	{
		if (g_emu_started)
		{
			if (Emu.GetStatus(false) != system_state::stopped)
			{
				Emu.GracefulShutdown(false, false);
				if (!wait_for_emulation_stop())
				{
					set_error("RPCS3 did not finish stopping emulation during core shutdown");
					g_lifecycle.finish_shutdown(false);
					return RPCS3_IOS_STOP_FAILED;
				}
			}
			jit_runtime::finalize();
			g_emu_started = false;
		}
		g_rpcn_client.reset();
		g_cfg_rpcn.clear_runtime_credentials();
		g_display_surface.clear();
		const auto jit_stats = rpcs3::ios::jit::get_statistics();
		emit_log(4, fmt::format(
			"Universal JIT arena peak usage: code %u MiB, data %u MiB of %u MiB each",
			jit_stats.peak_code_bytes / (1024 * 1024),
			jit_stats.peak_data_bytes / (1024 * 1024),
			jit_stats.capacity / (1024 * 1024)));
		logs::listener::sync_all();
		g_lifecycle.finish_shutdown(true);
		emit_log(4, "RPCS3Core shutdown completed");
		return RPCS3_IOS_OK;
	}
	catch (const std::exception& error)
	{
		set_error(error.what());
	}
	catch (...)
	{
		set_error("Unknown exception during RPCS3Core shutdown");
	}

	g_lifecycle.finish_shutdown(false);
	return RPCS3_IOS_INTERNAL_ERROR;
}

extern "C" const char* rpcs3_ios_last_error(void) noexcept
{
	thread_local std::string copy;
	copy = g_last_error.get();
	return copy.c_str();
}

[[noreturn]] void report_fatal_error(std::string_view text, bool, bool)
{
	set_error(std::string{text});
	emit_log(1, text);
	std::abort();
}
