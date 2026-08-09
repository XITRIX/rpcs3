#include "RPCS3IOS.h"
#include "RPCS3IOSCapabilities.h"
#include "RPCS3IOSContract.h"
#include "FirmwareInstaller.h"

#include "Emu/System.h"
#include "Emu/IdManager.h"
#include "Emu/Audio/Null/NullAudioBackend.h"
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
#include "Emu/RSX/Null/NullGSRender.h"
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
#include <chrono>
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
bool g_emu_started = false;

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

std::u32string ascii_to_u32(const char* fallback)
{
	std::u32string result;
	if (!fallback)
	{
		return result;
	}

	while (*fallback)
	{
		result.push_back(static_cast<unsigned char>(*fallback++));
	}
	return result;
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
	callbacks.get_gs_frame = []() -> std::unique_ptr<GSFrameBase> { return {}; };
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
		g_fxo->init<rsx::thread, named_thread<NullGSRender>>(archive);
	};
	callbacks.get_audio = []() -> std::shared_ptr<AudioBackend>
	{
		return std::make_shared<NullAudioBackend>();
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
	callbacks.get_localized_string = [](localized_string_id, const char* fallback)
	{
		return fallback ? std::string{fallback} : std::string{};
	};
	callbacks.get_localized_u32string = [](localized_string_id, const char* fallback)
	{
		return ascii_to_u32(fallback);
	};
	callbacks.get_localized_setting = [](const cfg::_base*, u32) { return std::string{}; };
	callbacks.get_photo_path = [](std::string_view name)
	{
		return fs::get_config_dir() + "photos/" + std::string{name};
	};
	callbacks.play_sound = [](const std::string&, std::optional<f32>) {};
	callbacks.get_image_info = [](const std::string&, std::string&, s32&, s32&, s32&) { return false; };
	callbacks.get_scaled_image = [](const std::string&, s32, s32, s32&, s32&, u8*, bool) { return false; };
	callbacks.resolve_path = [](std::string_view path) { return std::string{path}; };
	callbacks.get_font_dirs = []() { return std::vector<std::string>{}; };
	callbacks.on_install_pkgs = [](const std::vector<std::string>&) { return false; };
	callbacks.add_breakpoint = [](u32) {};
	callbacks.display_sleep_control_supported = []() { return false; };
	callbacks.enable_display_sleep = [](bool) {};
	callbacks.check_microphone_permissions = []() {};
	callbacks.make_video_source = []() -> std::unique_ptr<video_source> { return {}; };
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
}

extern "C" uint32_t rpcs3_ios_abi_version(void) noexcept
{
	return RPCS3_IOS_ABI_VERSION;
}

extern "C" const char* rpcs3_ios_build_info(void) noexcept
{
	return "{\"abi\":4,\"frontend\":\"ios\",\"upstream\":\"3d587726a23f514be0e7c3ac43e2db0cf2fe931a\",\"llvm\":\"ca7933e47d3a3451d81e72ac174dcb5aa28b59d1\",\"renderer\":\"null\",\"audio\":\"null\",\"input\":\"null\",\"media_codecs\":false}";
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

	try
	{
		g_application_support_path = config->application_support_path;
		g_cache_path = config->cache_path;
		g_config = *config;
		g_config.application_support_path = g_application_support_path.c_str();
		g_config.cache_path = g_cache_path.c_str();

		if (!fs::set_config_dir(g_config.application_support_path) || !fs::set_cache_dir(g_config.cache_path))
		{
			set_error("Unable to configure writable RPCS3 sandbox directories");
			g_lifecycle.finish_initialize(false);
			return RPCS3_IOS_INVALID_ARGUMENT;
		}

		g_log_listener = std::make_unique<callback_log_listener>();
		logs::listener::add(g_log_listener.get());
		Emu.SetCallbacks(make_callbacks());
		Emu.SetSupportedRenderers({video_renderer::null});
		Emu.SetDefaultRenderer(video_renderer::null);
		Emu.SetDefaultGraphicsAdapter({});
		Emu.SetHasGui(false);
		Emu.SetHeadless(true);
		Emu.SetUsr("00000001");
		g_emu_started = true;
		Emu.Init();
		g_lifecycle.finish_initialize(true);
		emit_log(4, "RPCS3 Emu.Init completed with the iOS null frontend");
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
	if (const auto result = g_lifecycle.begin_firmware_install(); result != RPCS3_IOS_OK)
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
		g_lifecycle.finish_firmware_install();
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

	g_lifecycle.finish_firmware_install();
	return RPCS3_IOS_FIRMWARE_INSTALL_FAILED;
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

	try
	{
		const std::string vsh_path = g_cfg_vfs.get_dev_flash() + "vsh/module/vsh.self";
		if (!fs::is_file(vsh_path))
		{
			set_error("PlayStation 3 firmware is missing vsh/module/vsh.self");
			return RPCS3_IOS_BOOT_FAILED;
		}

		emit_log(4, "Booting the PlayStation 3 XMB from installed firmware");
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
			detail = "Preparing firmware modules";
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
	std::lock_guard lock(g_api_mutex);
	copy = g_last_error.get();
	return copy.c_str();
}

[[noreturn]] void report_fatal_error(std::string_view text, bool, bool)
{
	set_error(std::string{text});
	emit_log(1, text);
	std::abort();
}
