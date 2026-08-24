#include "RPCS3IOSSettings.h"
#include "GameLibrary.h"

#include "Emu/system_config.h"
#include "Emu/system_utils.hpp"
#include "Utilities/File.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>

namespace rpcs3::ios
{
namespace
{
using kind = rpcs3_ios_setting_kind;

#define SETTING(KEY, CATEGORY, SECTION, NAME, DESCRIPTION, KIND, ENTRY, MINIMUM, MAXIMUM, STEP) \
	setting_record{KEY, CATEGORY, SECTION, NAME, DESCRIPTION, KIND, &ENTRY, MINIMUM, MAXIMUM, STEP, setting_scope::global_and_game}

#define GAME_SETTING(KEY, CATEGORY, SECTION, NAME, DESCRIPTION, KIND, ENTRY, MINIMUM, MAXIMUM, STEP) \
	setting_record{KEY, CATEGORY, SECTION, NAME, DESCRIPTION, KIND, &ENTRY, MINIMUM, MAXIMUM, STEP, setting_scope::game_only}

#define GLOBAL_SETTING(KEY, CATEGORY, SECTION, NAME, DESCRIPTION, KIND, ENTRY, MINIMUM, MAXIMUM, STEP) \
	setting_record{KEY, CATEGORY, SECTION, NAME, DESCRIPTION, KIND, &ENTRY, MINIMUM, MAXIMUM, STEP, setting_scope::global_only}

const std::array catalog{
	// CPU
	SETTING("cpu.thread_scheduler", "CPU", "General", "Thread scheduler", "Selects whether RPCS3 or iOS schedules emulated CPU threads.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.core.thread_scheduler, 0, 0, 0),
	GAME_SETTING("cpu.ppu_decoder", "CPU", "PPU", "PPU Decoder", "Selects static interpretation or LLVM recompilation for PPU code; the interpreter is intended for compatibility diagnosis and is substantially slower.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.core.ppu_decoder, 0, 0, 0),
	GAME_SETTING("cpu.ppu_profiler", "CPU", "PPU", "PPU Profiler", "Records sampled PPU guest-block addresses in the log when emulation stops; intended only for bounded compatibility diagnosis because it reduces performance.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.core.ppu_prof, 0, 1, 1),
	GAME_SETTING("cpu.spu_decoder", "CPU", "SPU", "SPU Decoder", "Selects static or dynamic interpretation or LLVM recompilation for SPU code; the interpreters are intended for compatibility diagnosis and are substantially slower.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.core.spu_decoder, 0, 0, 0),
	SETTING("cpu.spu_block_size", "CPU", "SPU", "SPU block size", "Controls how aggressively the SPU LLVM recompiler combines blocks.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.core.spu_block_size, 0, 0, 0),
	SETTING("cpu.spu_xfloat_accuracy", "CPU", "SPU", "SPU XFloat Accuracy", "Balances SPU floating-point accuracy and performance.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.core.spu_xfloat_accuracy, 0, 0, 0),
	SETTING("cpu.preferred_spu_threads", "CPU", "SPU", "Preferred SPU threads", "Reserves host threads for heavy simultaneous SPU workloads; zero is automatic.", kind::RPCS3_IOS_SETTING_INTEGER, g_cfg.core.preferred_spu_threads, 0, 6, 1),
	SETTING("cpu.spu_loop_detection", "CPU", "SPU", "Enable SPU loop detection", "Detects SPU wait loops and yields the host thread.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.core.spu_loop_detection, 0, 1, 1),
	SETTING("cpu.ppu_reservation_priority", "CPU", "SPU", "PPU Reservation Priority", "Prioritizes PPU reservations over competing SPU reservations.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.core.ppu_reservation_priority_over_spu, 0, 1, 1),

	// GPU
	SETTING("gpu.resolution", "GPU", "Display", "Default resolution", "Sets the resolution reported to PlayStation 3 software.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.video.resolution, 0, 0, 0),
	SETTING("gpu.aspect_ratio", "GPU", "Display", "Aspect ratio", "Sets the aspect ratio reported to PlayStation 3 software.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.video.aspect_ratio, 0, 0, 0),
	SETTING("gpu.frame_limit", "GPU", "Display", "Framelimit", "Limits presentation rate independently of the emulated game clock.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.video.frame_limit, 0, 0, 0),
	SETTING("gpu.vsync", "GPU", "Display", "VSync", "Controls Vulkan presentation synchronization through MoltenVK.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.video.vsync, 0, 0, 0),
	SETTING("gpu.stretch_to_display", "GPU", "Display", "Stretch to display area", "Stretches the rendered image to fill the available iOS video surface.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.stretch_to_display_area, 0, 1, 1),
	SETTING("gpu.output_scaling", "GPU", "Display", "Output Scaling", "Selects the final image scaling filter.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.video.output_scaling, 0, 0, 0),
	SETTING("gpu.msaa", "GPU", "Rendering", "Anti-aliasing", "Selects the PlayStation 3 multisampling behavior.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.video.antialiasing_level, 0, 0, 0),
	SETTING("gpu.shader_mode", "GPU", "Rendering", "Shader mode", "Controls shader compilation and interpretation behavior.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.video.shadermode, 0, 0, 0),
	SETTING("gpu.shader_precision", "GPU", "Rendering", "Shader quality", "Controls shader precision generated for the host GPU.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.video.shader_precision, 0, 0, 0),
	SETTING("gpu.resolution_scale", "GPU", "Rendering", "Resolution scale", "Scales 3D rendering resolution while leaving most 2D elements unchanged.", kind::RPCS3_IOS_SETTING_INTEGER, g_cfg.video.resolution_scale_percent, 25, 800, 25),
	SETTING("gpu.minimum_scalable_dimension", "GPU", "Rendering", "Resolution scale threshold", "Prevents small render targets from being resolution-scaled.", kind::RPCS3_IOS_SETTING_INTEGER, g_cfg.video.min_scalable_dimension, 1, 1024, 1),
	SETTING("gpu.anisotropic_filter", "GPU", "Rendering", "Anisotropic filter", "Overrides anisotropic filtering; zero leaves the game setting unchanged.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.video.anisotropic_level_override, 0, 16, 1),
	SETTING("gpu.texture_lod_bias", "GPU", "Rendering", "LOD bias offset", "Adds a bias to texture mip-level selection.", kind::RPCS3_IOS_SETTING_DECIMAL, g_cfg.video.texture_lod_bias, -32, 32, 0.25),
	SETTING("gpu.fsr_sharpening", "GPU", "Rendering", "RCAS Sharpening Strength", "Sets FidelityFX CAS sharpening intensity.", kind::RPCS3_IOS_SETTING_INTEGER, g_cfg.video.rcas_sharpening_intensity, 0, 100, 1),
	SETTING("gpu.write_color_buffers", "GPU", "Rendering", "Write color buffers", "Mirrors color buffers into emulated memory for games that read them from the CPU.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.write_color_buffers, 0, 1, 1),
	SETTING("gpu.strict_rendering", "GPU", "Rendering", "Strict rendering mode", "Disables selected rendering approximations for compatibility.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.strict_rendering_mode, 0, 1, 1),
	SETTING("gpu.multithreaded_rsx", "GPU", "Rendering", "Multithreaded RSX", "Uses an additional host thread for selected RSX work.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.multithreaded_rsx, 0, 1, 1),
	SETTING("gpu.async_texture_uploads", "GPU", "Rendering", "Asynchronous texture streaming", "Uploads Vulkan textures asynchronously when supported by MoltenVK.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.vk.asynchronous_texture_streaming, 0, 1, 1),
	SETTING("gpu.zcull_accuracy", "GPU", "Rendering", "ZCULL accuracy", "Selects precise, approximate, or relaxed ZCULL synchronization.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.video.precise_zpass_count, 0, 0, 0),

	// Audio
	SETTING("audio.format", "Audio", "Output", "Audio format", "Selects the channel format exposed by the emulated console.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.audio.format, 0, 0, 0),
	SETTING("audio.provider", "Audio", "Output", "Audio Provider", "Selects the emulated PlayStation 3 audio source.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.audio.provider, 0, 0, 0),
	SETTING("audio.avport", "Audio", "Output", "RSXAudio Avport", "Selects the emulated AV output used by RSXAudio.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.audio.rsxaudio_port, 0, 0, 0),
	SETTING("audio.channel_layout", "Audio", "Output", "Audio Output Format", "Chooses how RPCS3 mixes channels before the iOS RemoteIO backend.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.audio.channel_layout, 0, 0, 0),
	SETTING("audio.convert_to_16_bit", "Audio", "Output", "Convert to 16-bit", "Converts the final mix to signed 16-bit samples.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.audio.convert_to_s16, 0, 1, 1),
	SETTING("audio.master_volume", "Audio", "Playback", "Master", "Sets RPCS3 output volume before iOS system volume is applied.", kind::RPCS3_IOS_SETTING_INTEGER, g_cfg.audio.volume, 0, 200, 1),
	SETTING("audio.enable_buffering", "Audio", "Playback", "Enable buffering", "Buffers audio to reduce underruns.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.audio.enable_buffering, 0, 1, 1),
	SETTING("audio.buffer_duration", "Audio", "Playback", "Audio buffer duration", "Sets the desired output buffer duration in milliseconds.", kind::RPCS3_IOS_SETTING_INTEGER, g_cfg.audio.desired_buffer_duration, 4, 250, 1),
	SETTING("audio.time_stretching", "Audio", "Playback", "Enable time stretching", "Adjusts audio timing to conceal emulation speed variation.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.audio.enable_time_stretching, 0, 1, 1),
	SETTING("audio.time_stretching_threshold", "Audio", "Playback", "Time stretching threshold", "Sets the buffer threshold at which time stretching begins.", kind::RPCS3_IOS_SETTING_INTEGER, g_cfg.audio.time_stretching_threshold, 0, 100, 1),

	// System
	SETTING("system.language", "System", "Console", "Console Language", "Sets the language reported by the emulated console.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.sys.language, 0, 0, 0),
	SETTING("system.license_area", "System", "Console", "Console Region", "Sets the emulated console license region.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.sys.license_area, 0, 0, 0),
	SETTING("system.keyboard_type", "System", "Console", "Keyboard Type", "Sets the keyboard mapping reported to PlayStation 3 software.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.sys.keyboard_type, 0, 0, 0),
	SETTING("system.enter_button", "System", "Console", "Enter Button Assignment", "Chooses whether Cross or Circle confirms selections.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.sys.enter_button_assignment, 0, 0, 0),
	SETTING("system.date_format", "System", "Console", "Date Format", "Sets the emulated console date format.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.sys.date_fmt, 0, 0, 0),
	SETTING("system.time_format", "System", "Console", "Time Format", "Sets 12-hour or 24-hour console time.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.sys.time_fmt, 0, 0, 0),
	SETTING("system.time_offset", "System", "Console", "Console Time", "Offsets emulated console time from the iOS clock, in seconds.", kind::RPCS3_IOS_SETTING_INTEGER, g_cfg.sys.console_time_offset, -3153600000.0, 3153600000.0, 60),
	SETTING("system.empty_hdd_tmp", "System", "Storage", "Empty /dev_hdd0/tmp/", "Empties /dev_hdd0/tmp when a title starts.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.vfs.empty_hdd0_tmp, 0, 1, 1),
	SETTING("system.limit_cache", "System", "Storage", "Clear cache automatically", "Removes old disk-cache entries when the configured limit is exceeded.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.vfs.limit_cache_size, 0, 1, 1),
	SETTING("system.cache_size", "System", "Storage", "Maximum size", "Sets the disk-cache limit in megabytes.", kind::RPCS3_IOS_SETTING_INTEGER, g_cfg.vfs.cache_max_size, 0, 10240, 128),

	// Network
	SETTING("network.internet_status", "Network", "Connectivity", "Network status", "Reports the emulated console as connected or disconnected.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.net.net_active, 0, 0, 0),
	SETTING("network.dns", "Network", "Connectivity", "DNS", "Sets the DNS server used by emulated networking.", kind::RPCS3_IOS_SETTING_TEXT, g_cfg.net.dns, 0, 0, 0),
	SETTING("network.derive_mac", "Network", "Connectivity", "Derive ethernet address from PSID", "Derives the emulated MAC address from the console PSID.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.net.derive_mac_from_psid, 0, 1, 1),
	SETTING("network.psn_status", "Network", "PlayStation Network", "PSN status", "Selects disconnected, simulated PSN, or RPCN behavior.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.net.psn_status, 0, 0, 0),
	SETTING("network.psn_country", "Network", "PlayStation Network", "Country", "Sets the country reported to network services.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.net.country, 0, 0, 0),
	SETTING("network.clans", "Network", "PlayStation Network", "Enable Clans", "Enables RPCN clan support where available.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.net.clans_enabled, 0, 1, 1),

	// Advanced
	SETTING("advanced.debug_console_mode", "Advanced", "CPU Compatibility", "Debug console mode", "Emulates additional debug-console behavior required by some software.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.core.debug_console_mode, 0, 1, 1),
	SETTING("advanced.accurate_spu_dma", "Advanced", "CPU Compatibility", "Accurate SPU DMA", "Uses stricter ordering for SPU DMA operations.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.core.spu_accurate_dma, 0, 1, 1),
	SETTING("advanced.accurate_rsx_access", "Advanced", "CPU Compatibility", "Accurate RSX reservation access", "Uses stricter reservation behavior for CPU access shared with RSX.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.core.rsx_accurate_res_access, 0, 1, 1),
	SETTING("advanced.ppu_vnan_fixup", "Advanced", "CPU Compatibility", "Approximate PPU Vector NaN Handling", "Enables partial correction of PPU vector NaN behavior.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.core.ppu_fix_vnan, 0, 1, 1),
	SETTING("advanced.llvm_precompilation", "Advanced", "CPU Compatibility", "PPU/SPU LLVM Precompilation", "Precompiles PPU modules before starting a title.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.core.llvm_precompilation, 0, 1, 1),
	SETTING("advanced.savestate_suspend", "Advanced", "Savestates", "Anti-Cheat Savestates Mode", "Closes emulation while saving and deletes the save after loading.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.savestate.suspend_emu, 0, 1, 1),
	SETTING("advanced.savestate_compatible", "Advanced", "Savestates", "SPU Compatible Savestates Mode", "Improves SPU LLVM savestate reliability. Takes effect on the next boot and can reduce performance.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.savestate.compatible_mode, 0, 1, 1),
	SETTING("advanced.spu_profiler", "Advanced", "Diagnostics", "SPU Profiler", "Collects SPU profiling information at a performance cost.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.core.spu_prof, 0, 1, 1),
	SETTING("advanced.silence_logs", "Advanced", "Diagnostics", "Silence all logs", "Suppresses RPCS3 log messages sent to the Debug tab.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.misc.silence_all_logs, 0, 1, 1),
	SETTING("advanced.read_color_buffers", "Advanced", "GPU Compatibility", "Read color buffers", "Allows the CPU to read rendered color-buffer data.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.read_color_buffers, 0, 1, 1),
	SETTING("advanced.read_depth_buffer", "Advanced", "GPU Compatibility", "Read depth buffers", "Allows the CPU to read rendered depth-buffer data.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.read_depth_buffer, 0, 1, 1),
	SETTING("advanced.write_depth_buffer", "Advanced", "GPU Compatibility", "Write depth buffers", "Mirrors depth-buffer data into emulated memory.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.write_depth_buffer, 0, 1, 1),
	SETTING("advanced.handle_tiled_memory", "Advanced", "GPU Compatibility", "Handle RSX memory tiling", "Handles tiled RSX memory layouts for titles that access them directly.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.handle_tiled_memory, 0, 1, 1),
	SETTING("advanced.disable_msl_fast_math", "Advanced", "MoltenVK", "Disable MSL Fast Math", "Requests stricter Metal Shading Language floating-point behavior.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.disable_msl_fast_math, 0, 1, 1),
	SETTING("advanced.disable_async_memory", "Advanced", "MoltenVK", "Disable Asynchronous Memory Manager", "Disables asynchronous host-memory management for compatibility.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.disable_async_host_memory_manager, 0, 1, 1),
	SETTING("advanced.disable_texel_remapping", "Advanced", "MoltenVK", "Disable Hardware ColorSpace Remapping", "Uses shader-based color-space remapping instead of host hardware paths.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.disable_hardware_texel_remapping, 0, 1, 1),
	SETTING("advanced.mfc_shuffling", "Advanced", "Scheduling", "Delay each odd MFC Command", "Delays and reorders one MFC command for compatibility.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.core.mfc_transfers_shuffling, 0, 1, 1),
	SETTING("advanced.disable_spu_spin", "Advanced", "Scheduling", "Disable SPU GETLLAR Spin Optimization", "Disables GETLLAR spin-loop optimization.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.core.spu_getllar_spin_optimization_disabled, 0, 1, 1),
	SETTING("advanced.spu_busy_loop", "Advanced", "Scheduling", "Enable SPU Events Busy Loop", "Enables busy waiting for selected SPU reservation events.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.core.spu_reservation_busy_waiting_enabled, 0, 1, 1),
	SETTING("advanced.max_spurs_threads", "Advanced", "Scheduling", "Maximum SPURS threads", "Limits active SPURS threads; six is the default unlimited behavior.", kind::RPCS3_IOS_SETTING_INTEGER, g_cfg.core.max_spurs_threads, 1, 6, 1),
	SETTING("advanced.sleep_timers", "Advanced", "Scheduling", "Sleep timers accuracy", "Controls how aggressively RPCS3 corrects host sleep timing.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.core.sleep_timers_accuracy, 0, 0, 0),
	SETTING("advanced.fifo_accuracy", "Advanced", "Scheduling", "RSX FIFO accuracy", "Controls synchronization accuracy when fetching RSX commands.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.core.rsx_fifo_accuracy, 0, 0, 0),
	GAME_SETTING("advanced.driver_wakeup_delay", "Advanced", "Scheduling", "Driver wake-up delay", "Delays RSX wake-ups to prevent FIFO desynchronization in affected titles; may reduce performance.", kind::RPCS3_IOS_SETTING_INTEGER, g_cfg.video.driver_wakeup_delay, 0, 800, 20),
	SETTING("advanced.max_preempt_count", "Advanced", "Scheduling", "Max Power Saving CPU-preemptions", "Limits CPU thread preemptions per frame; zero is automatic.", kind::RPCS3_IOS_SETTING_INTEGER, g_cfg.core.max_cpu_preempt_count_per_frame, 0, 400, 10),

	// Experimental choices are resolved once after the effective title config
	// loads. Render and SPU hot paths never read these cfg entries.
	SETTING("experimental.neon_byte_swap", "Experimental", "ARM64 Uploads", "ARM64 byte-swap uploads", "Uses a four-lane NEON path for swapped RSX uploads. Takes effect on the next boot.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.ios_experimental.neon_byte_swap, 0, 0, 0),
	SETTING("experimental.neon_primitive_restart", "Experimental", "ARM64 Uploads", "ARM64 primitive-restart uploads", "Uses NEON min/max and restart filtering for index uploads. Takes effect on the next boot.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.ios_experimental.neon_primitive_restart, 0, 0, 0),
	SETTING("experimental.precomputed_indices", "Experimental", "ARM64 Uploads", "Precomputed non-native indices", "Copies cached quad and triangle-fan index patterns instead of regenerating them for each draw. Takes effect on the next boot.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.ios_experimental.precomputed_indices, 0, 0, 0),
	SETTING("experimental.mobile_spu_scheduling", "Experimental", "SPU", "Mobile SPU compile scheduling", "Uses a nonzero compile-throttle floor on low-core mobile CPUs. Automatic currently preserves upstream behavior.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.ios_experimental.mobile_spu_scheduling, 0, 0, 0),
	SETTING("experimental.fifo_cache_size", "Experimental", "RSX FIFO", "RSX FIFO read cache", "Selects a 1 KiB compatibility cache or a 4 KiB cache with fewer refills. Takes effect on the next boot.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.ios_experimental.fifo_cache_size, 0, 0, 0),
	SETTING("experimental.fifo_idle_mode", "Experimental", "RSX FIFO", "RSX FIFO idle wait", "Chooses scheduler yielding or an ARM event wait after a short empty-ring spin window.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.ios_experimental.fifo_idle_mode, 0, 0, 0),
	SETTING("experimental.deferred_get_publishing", "Experimental", "RSX FIFO", "Deferred FIFO GET publishing", "Publishes guest FIFO progress every eighth packet and always before an idle or blocking path. Automatic is disabled.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.ios_experimental.deferred_get_publishing, 0, 0, 0),
	SETTING("experimental.getllar_backoff", "Experimental", "SPU", "GETLLAR mobile backoff", "Memoizes stack classification and stops repeated out-buffer verdicts from suppressing SPU backoff forever. Automatic is disabled.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.ios_experimental.getllar_backoff, 0, 0, 0),
	GLOBAL_SETTING("experimental.persistent_spu_object_cache", "Experimental", "SPU", "Persistent SPU object cache", "Reuses configuration-keyed LLVM objects between launches. Global only; Automatic is disabled.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.ios_experimental.persistent_spu_object_cache, 0, 0, 0),

	// Emulator
	SETTING("emulator.max_llvm_threads", "Emulator", "Compilation", "Max LLVM Compile Threads", "Limits concurrent LLVM compilation threads; zero uses the iOS memory-safe automatic limit.", kind::RPCS3_IOS_SETTING_INTEGER, g_cfg.core.llvm_threads, 0, 64, 1),
	SETTING("emulator.shader_compiler_threads", "Emulator", "Compilation", "Max Shader Compile Threads", "Limits concurrent shader compiler threads; zero is automatic.", kind::RPCS3_IOS_SETTING_INTEGER, g_cfg.video.shader_compiler_threads_count, 0, 16, 1),
	// Temporarily hidden until SwiftUI guest-dialog fallbacks are complete.
	// SETTING("emulator.native_interface", "Emulator", "Behavior", "Use native user interface", "Uses RPCS3's controller-driven in-game overlays instead of desktop dialogs.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.misc.use_native_interface, 0, 1, 1),
	SETTING("emulator.prevent_display_sleep", "Emulator", "Behavior", "Prevent display sleep while running games", "Keeps the iOS display awake while emulation is running.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.misc.prevent_display_sleep, 0, 1, 1),
	SETTING("emulator.pause_home_menu", "Emulator", "Behavior", "Pause emulation during home menu", "Pauses emulation whenever RPCS3's native home menu is open.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.misc.pause_during_home_menu, 0, 1, 1),
	SETTING("emulator.play_boot_music", "Emulator", "Behavior", "Play music during boot sequence", "Plays title-provided music during the boot sequence when available.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.misc.play_music_during_boot, 0, 1, 1),
	SETTING("emulator.start_savestate_paused", "Emulator", "Behavior", "Pause emulation after loading savestates", "Pauses on the first frame after a savestate is loaded.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.savestate.start_paused, 0, 1, 1),
	SETTING("emulator.show_rpcn_popups", "Emulator", "Native Overlays", "Show netplay popups", "Shows RPCN friend-list notifications in the native overlay.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.misc.show_rpcn_popups, 0, 1, 1),
	SETTING("emulator.show_shader_hint", "Emulator", "Native Overlays", "Show shader compilation hint", "Shows a native overlay while shaders compile.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.misc.show_shader_compilation_hint, 0, 1, 1),
	SETTING("emulator.show_ppu_hint", "Emulator", "Native Overlays", "Show PPU compilation hint", "Shows a native overlay while PPU modules compile.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.misc.show_ppu_compilation_hint, 0, 1, 1),
	SETTING("emulator.show_autosave_hint", "Emulator", "Native Overlays", "Show autosave/autoload hint", "Shows native save-data activity indicators.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.misc.show_autosave_autoload_hint, 0, 1, 1),
	SETTING("emulator.show_pressure_hint", "Emulator", "Native Overlays", "Show pressure intensity toggle hint", "Shows a hint when controller pressure-intensity mode changes.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.misc.show_pressure_intensity_toggle_hint, 0, 1, 1),
	SETTING("emulator.show_limiter_hint", "Emulator", "Native Overlays", "Show analog limiter toggle hint", "Shows a hint when the analog limiter changes.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.misc.show_analog_limiter_toggle_hint, 0, 1, 1),
	SETTING("emulator.show_fatal_hints", "Emulator", "Native Overlays", "Show fatal error hints", "Shows fatal core errors in RPCS3's native overlay.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.misc.show_fatal_error_hints, 0, 1, 1),
	SETTING("emulator.perf_enabled", "Emulator", "Performance Overlay", "Enable performance overlay", "Shows RPCS3 performance metrics over the rendered title.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.perf_overlay.enabled, 0, 1, 1),
	SETTING("emulator.perf_framerate_graph", "Emulator", "Performance Overlay", "Show framerate graph", "Shows the framerate history graph.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.perf_overlay.framerate_graph_enabled, 0, 1, 1),
	SETTING("emulator.perf_frametime_graph", "Emulator", "Performance Overlay", "Show frametime graph", "Shows the frametime history graph.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.perf_overlay.frametime_graph_enabled, 0, 1, 1),
	SETTING("emulator.perf_framerate_points", "Emulator", "Performance Overlay", "Framerate datapoints", "Sets the number of values retained by the framerate graph.", kind::RPCS3_IOS_SETTING_INTEGER, g_cfg.video.perf_overlay.framerate_datapoint_count, 2, 6000, 1),
	SETTING("emulator.perf_frametime_points", "Emulator", "Performance Overlay", "Frametime datapoints", "Sets the number of values retained by the frametime graph.", kind::RPCS3_IOS_SETTING_INTEGER, g_cfg.video.perf_overlay.frametime_datapoint_count, 2, 6000, 1),
	SETTING("emulator.perf_detail", "Emulator", "Performance Overlay", "Detail Level", "Controls the amount of performance information shown.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.video.perf_overlay.level, 0, 0, 0),
	SETTING("emulator.perf_position", "Emulator", "Performance Overlay", "Position", "Places the performance overlay in a screen quadrant.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.video.perf_overlay.position, 0, 0, 0),
	SETTING("emulator.perf_update", "Emulator", "Performance Overlay", "Update Interval", "Sets the metrics refresh interval in milliseconds.", kind::RPCS3_IOS_SETTING_INTEGER, g_cfg.video.perf_overlay.update_interval, 1, 1000, 1),
	SETTING("emulator.perf_font_size", "Emulator", "Performance Overlay", "Font Size", "Sets performance overlay text size in pixels.", kind::RPCS3_IOS_SETTING_INTEGER, g_cfg.video.perf_overlay.font_size, 4, 36, 1),
	SETTING("emulator.perf_opacity", "Emulator", "Performance Overlay", "Opacity", "Sets performance overlay opacity as a percentage.", kind::RPCS3_IOS_SETTING_INTEGER, g_cfg.video.perf_overlay.opacity, 0, 100, 1),
	SETTING("emulator.perf_margin_x", "Emulator", "Performance Overlay", "Horizontal Margin", "Sets horizontal distance from the selected screen edge.", kind::RPCS3_IOS_SETTING_DECIMAL, g_cfg.video.perf_overlay.margin_x, 0, 100, 0.25),
	SETTING("emulator.perf_margin_y", "Emulator", "Performance Overlay", "Vertical Margin", "Sets vertical distance from the selected screen edge.", kind::RPCS3_IOS_SETTING_DECIMAL, g_cfg.video.perf_overlay.margin_y, 0, 100, 0.25),
	SETTING("emulator.perf_center_x", "Emulator", "Performance Overlay", "Centered", "Centers the overlay horizontally and ignores horizontal margin.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.perf_overlay.center_x, 0, 1, 1),
	SETTING("emulator.perf_center_y", "Emulator", "Performance Overlay", "Centered", "Centers the overlay vertically and ignores vertical margin.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.perf_overlay.center_y, 0, 1, 1),
	SETTING("emulator.perf_window_space", "Emulator", "Performance Overlay", "Use Window Space", "Allows the overlay to occupy letterboxed space outside the game image.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.perf_overlay.use_window_space, 0, 1, 1),
	SETTING("emulator.shader_background", "Emulator", "Shader Loading", "Allow custom background", "Allows title artwork behind native shader-loading messages.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.shader_preloading_dialog.use_custom_background, 0, 1, 1),
	SETTING("emulator.shader_darkening", "Emulator", "Shader Loading", "Background darkening", "Sets shader-loading background darkening intensity.", kind::RPCS3_IOS_SETTING_INTEGER, g_cfg.video.shader_preloading_dialog.darkening_strength, 0, 100, 1),
	SETTING("emulator.shader_blur", "Emulator", "Shader Loading", "Background blur", "Sets shader-loading background blur intensity.", kind::RPCS3_IOS_SETTING_INTEGER, g_cfg.video.shader_preloading_dialog.blur_strength, 0, 100, 1),

	// Debug
	SETTING("debug.force_high_precision_z", "Debug", "GPU", "Use High Precision Z-Buffer", "Forces high-precision depth buffers for compatibility testing.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.force_high_precision_z_buffer, 0, 1, 1),
	SETTING("debug.vulkan_output", "Debug", "GPU", "Debug Output", "Enables additional renderer diagnostics in the Debug tab.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.debug_output, 0, 1, 1),
	SETTING("debug.renderer_overlay", "Debug", "GPU", "Debug Overlay", "Displays internal RSX renderer diagnostics.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.debug_overlay, 0, 1, 1),
	SETTING("debug.log_shaders", "Debug", "GPU", "Log Shader Programs", "Writes generated shader programs into RPCS3's sandbox.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.log_programs, 0, 1, 1),
	SETTING("debug.disable_occlusion", "Debug", "GPU", "Disable ZCull occlusion queries", "Disables host ZCULL occlusion queries.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.disable_zcull_queries, 0, 1, 1),
	SETTING("debug.disable_video", "Debug", "GPU", "Disable Video Output", "Runs RSX work without presenting game frames.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.disable_video_output, 0, 1, 1),
	SETTING("debug.force_cpu_blit", "Debug", "GPU", "Force CPU blit emulation", "Moves blit processing from the GPU to the CPU.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.force_cpu_blit_processing, 0, 1, 1),
	SETTING("debug.disable_vma", "Debug", "GPU", "Disable Vulkan Memory Allocator", "Uses RPCS3's fallback Vulkan allocation paths.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.disable_vulkan_mem_allocator, 0, 1, 1),
	SETTING("debug.disable_fifo_reordering", "Debug", "GPU", "Disable FIFO Reordering", "Disables command FIFO reordering optimizations.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.disable_FIFO_reordering, 0, 1, 1),
	SETTING("debug.strict_texture_flushing", "Debug", "GPU", "Strict Texture Flushing", "Uses stricter synchronization for texture cache invalidation.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.strict_texture_flushing, 0, 1, 1),
	SETTING("debug.gpu_texture_scaling", "Debug", "GPU", "Force GPU Texture Scaling", "Moves texture scaling work to the host GPU.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.use_gpu_texture_scaling, 0, 1, 1),
	SETTING("debug.host_gpu_labels", "Debug", "GPU", "Allow Host GPU labels", "Uses host-visible GPU labels for synchronization.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.host_label_synchronization, 0, 1, 1),
	SETTING("debug.disable_vertex_cache", "Debug", "GPU", "Disable vertex cache", "Disables reuse of transformed vertex data.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.disable_vertex_cache, 0, 1, 1),
	SETTING("debug.emulate_depth_compare", "Debug", "GPU", "Emulate Special Depth Comparison", "Emulates special depth comparison modes in shaders.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.emulate_depth_compare, 0, 1, 1),
	SETTING("debug.force_msaa_resolve", "Debug", "GPU", "Force Hardware MSAA Resolve", "Forces host hardware multisample resolve paths.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.force_hw_MSAA_resolve, 0, 1, 1),
	SETTING("debug.disable_shader_cache", "Debug", "GPU", "Disable On-Disk Shader Cache", "Prevents compiled shaders from being persisted between launches.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.video.disable_on_disk_shader_cache, 0, 1, 1),
	SETTING("debug.framebuffer_aliasing", "Debug", "GPU", "Framebuffer Aliasing Heuristic Bias", "Chooses how ambiguous framebuffer aliases are interpreted.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.video.fb_aliasing_bias, 0, 0, 0),
	SETTING("debug.vulkan_scheduler", "Debug", "GPU", "Vulkan Queue Scheduler", "Selects safe or fast asynchronous queue scheduling.", kind::RPCS3_IOS_SETTING_CHOICE, g_cfg.video.vk.asynchronous_scheduler, 0, 0, 0),
	SETTING("debug.ppu", "Debug", "CPU", "PPU Debug", "Enables PPU debugger instrumentation.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.core.ppu_debug, 0, 1, 1),
	SETTING("debug.spu", "Debug", "CPU", "SPU Debug", "Enables SPU debugger instrumentation.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.core.spu_debug, 0, 1, 1),
	SETTING("debug.mfc", "Debug", "CPU", "MFC Debug", "Enables MFC debugger instrumentation.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.core.mfc_debug, 0, 1, 1),
	SETTING("debug.ppu_saturation", "Debug", "CPU Accuracy", "Accurate PPU Saturation Bit", "Emulates PPU floating-point saturation flags.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.core.ppu_set_sat_bit, 0, 1, 1),
	SETTING("debug.cache_line_stores", "Debug", "CPU Accuracy", "Accurate PPU/SPU Cache Line Stores", "Uses strict semantics for PPU cache-line stores.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.core.accurate_cache_line_stores, 0, 1, 1),
	SETTING("debug.hook_static_functions", "Debug", "CPU", "Hook static functions", "Attempts to replace recognized statically linked functions with HLE implementations.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.core.hook_functions, 0, 1, 1),
	SETTING("debug.performance_report", "Debug", "CPU", "Enable performance report", "Logs unusually slow emulated operations.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.core.perf_report, 0, 1, 1),
	SETTING("debug.ppu_128_loop", "Debug", "CPU Accuracy", "Accurate PPU 128 reservations", "Sets the maximum loop length that receives accurate 128-byte reservation behavior.", kind::RPCS3_IOS_SETTING_INTEGER, g_cfg.core.ppu_128_reservations_loop_max_length, -1, 14, 1),
	SETTING("debug.ppu_threads", "Debug", "CPU", "PPU Thread Count", "Sets the number of simultaneously scheduled PPU threads.", kind::RPCS3_IOS_SETTING_INTEGER, g_cfg.core.ppu_threads, 1, 8, 1),
	SETTING("debug.pad_overlay", "Debug", "Input", "Debug Overlay For Pad Input", "Displays raw iOS pad input received by RPCS3.", kind::RPCS3_IOS_SETTING_BOOLEAN, g_cfg.io.pad_debug_overlay, 0, 1, 1),
};

#undef SETTING
#undef GAME_SETTING
#undef GLOBAL_SETTING

constexpr std::size_t maximum_preset_name_size = 80;
constexpr std::size_t maximum_preset_config_size = 1024 * 1024;
constexpr std::size_t maximum_presets_per_title = 128;
constexpr std::string_view preset_filename_prefix = "preset_";
constexpr std::string_view preset_filename_suffix = ".yml";

game_settings_preset_result preset_failure(game_settings_preset_error error, std::string detail)
{
	return {error, std::move(detail)};
}

bool is_valid_preset_name(std::string_view name)
{
	if (name.empty() || name.size() > maximum_preset_name_size ||
		name.front() == ' ' || name.back() == ' ' || name == "." || name == "..")
	{
		return false;
	}

	return std::ranges::none_of(name, [](unsigned char character)
	{
		return character < 0x20 || character == 0x7f ||
			character == '/' || character == '\\' || character == ':';
	});
}

std::string ascii_folded(std::string_view value)
{
	std::string folded{value};
	std::ranges::transform(folded, folded.begin(), [](unsigned char character)
	{
		return static_cast<char>(std::tolower(character));
	});
	return folded;
}

std::string encoded_preset_filename(std::string_view name)
{
	constexpr char digits[] = "0123456789abcdef";
	std::string filename;
	filename.reserve(preset_filename_prefix.size() + name.size() * 2 + preset_filename_suffix.size());
	filename.append(preset_filename_prefix);
	for (const unsigned char character : name)
	{
		filename.push_back(digits[character >> 4]);
		filename.push_back(digits[character & 0xf]);
	}
	filename.append(preset_filename_suffix);
	return filename;
}

std::optional<unsigned char> decode_hex_digit(char character)
{
	if (character >= '0' && character <= '9')
	{
		return static_cast<unsigned char>(character - '0');
	}
	if (character >= 'a' && character <= 'f')
	{
		return static_cast<unsigned char>(character - 'a' + 10);
	}
	return std::nullopt;
}

std::optional<std::string> decoded_preset_name(std::string_view filename)
{
	if (!filename.starts_with(preset_filename_prefix) ||
		!filename.ends_with(preset_filename_suffix))
	{
		return std::nullopt;
	}

	filename.remove_prefix(preset_filename_prefix.size());
	filename.remove_suffix(preset_filename_suffix.size());
	if (filename.empty() || filename.size() % 2 != 0)
	{
		return std::nullopt;
	}

	std::string name;
	name.reserve(filename.size() / 2);
	for (std::size_t index = 0; index < filename.size(); index += 2)
	{
		const auto high = decode_hex_digit(filename[index]);
		const auto low = decode_hex_digit(filename[index + 1]);
		if (!high || !low)
		{
			return std::nullopt;
		}
		name.push_back(static_cast<char>((*high << 4) | *low));
	}
	return is_valid_preset_name(name) ? std::optional{std::move(name)} : std::nullopt;
}

std::string preset_path(std::string_view title_id, std::string_view name)
{
	return game_settings_preset_directory(title_id) + encoded_preset_filename(name);
}

game_settings_preset_result read_preset_records(
	std::string_view title_id,
	std::vector<game_settings_preset>& presets)
{
	presets.clear();
	const std::string directory = game_settings_preset_directory(title_id);
	if (!fs::exists(directory))
	{
		return fs::g_tls_error == fs::error::noent
			? game_settings_preset_result{}
			: preset_failure(
				game_settings_preset_error::storage_failed,
				"RPCS3 could not inspect the game-settings preset directory");
	}
	if (!fs::is_dir(directory))
	{
		return preset_failure(
			game_settings_preset_error::storage_failed,
			"The game-settings preset path is not a directory");
	}

	fs::dir entries{directory};
	if (!entries)
	{
		return preset_failure(
			game_settings_preset_error::storage_failed,
			"RPCS3 could not open the game-settings preset directory");
	}

	for (const auto& entry : entries)
	{
		if (entry.is_directory || entry.is_symlink)
		{
			continue;
		}
		auto name = decoded_preset_name(entry.name);
		if (!name)
		{
			continue;
		}
		if (presets.size() >= maximum_presets_per_title)
		{
			presets.clear();
			return preset_failure(
				game_settings_preset_error::too_many_presets,
				"A game may have at most 128 settings presets");
		}
		presets.push_back({
			std::move(*name),
			entry.size,
			entry.mtime,
		});
	}

	std::ranges::sort(presets, [](const game_settings_preset& lhs, const game_settings_preset& rhs)
	{
		return ascii_folded(lhs.name) < ascii_folded(rhs.name);
	});
	return {};
}

game_settings_preset_result validate_new_preset_name(
	std::string_view title_id,
	std::string_view name,
	std::string_view excluded_name = {})
{
	if (!is_valid_preset_name(name))
	{
		return preset_failure(
			game_settings_preset_error::invalid_name,
			"Preset names must be 1-80 UTF-8 bytes, may not start or end with a space, and may not contain control characters, '/', '\\', or ':'");
	}

	std::vector<game_settings_preset> presets;
	if (auto result = read_preset_records(title_id, presets); !result)
	{
		return result;
	}
	const std::string folded_name = ascii_folded(name);
	for (const auto& preset : presets)
	{
		if (preset.name != excluded_name && ascii_folded(preset.name) == folded_name)
		{
			return preset_failure(
				game_settings_preset_error::already_exists,
				"A settings preset with this name already exists");
		}
	}
	if (presets.size() >= maximum_presets_per_title && excluded_name.empty())
	{
		return preset_failure(
			game_settings_preset_error::too_many_presets,
			"A game may have at most 128 settings presets");
	}
	return {};
}

game_settings_preset_result locate_preset(
	std::string_view title_id,
	std::string_view requested_name,
	game_settings_preset& preset)
{
	if (!is_valid_preset_name(requested_name))
	{
		return preset_failure(
			game_settings_preset_error::invalid_name,
			"The settings preset name is invalid");
	}

	std::vector<game_settings_preset> presets;
	if (auto result = read_preset_records(title_id, presets); !result)
	{
		return result;
	}
	const std::string folded_name = ascii_folded(requested_name);
	const auto found = std::ranges::find_if(presets, [&](const game_settings_preset& candidate)
	{
		return ascii_folded(candidate.name) == folded_name;
	});
	if (found == presets.end())
	{
		return preset_failure(
			game_settings_preset_error::not_found,
			"The requested game-settings preset was not found");
	}
	preset = *found;
	return {};
}

game_settings_preset_result read_validated_configuration(
	std::string_view path,
	std::string& content)
{
	fs::file source{std::string{path}, fs::read};
	if (!source)
	{
		return preset_failure(
			game_settings_preset_error::storage_failed,
			"RPCS3 could not open the settings preset");
	}
	const std::uint64_t size = source.size();
	if (size == 0 || size > maximum_preset_config_size)
	{
		return preset_failure(
			game_settings_preset_error::invalid_config,
			"Settings presets must contain 1 byte to 1 MiB of YAML");
	}
	content = source.to_string();
	cfg_root verifier;
	if (content.size() != size || !verifier.validate(content))
	{
		content.clear();
		return preset_failure(
			game_settings_preset_error::invalid_config,
			"The selected file is not a valid RPCS3 configuration");
	}
	return {};
}

game_settings_preset_result write_configuration(
	std::string_view path,
	std::string_view content)
{
	fs::pending_file destination{std::string{path}};
	if (!destination.file ||
		destination.file.write(content.data(), content.size()) != content.size() ||
		!destination.commit())
	{
		return preset_failure(
			game_settings_preset_error::storage_failed,
			"RPCS3 could not atomically save the settings preset");
	}
	return {};
}
}

std::span<const setting_record> settings_catalog() noexcept
{
	return catalog;
}

std::string game_settings_preset_directory(std::string_view title_id)
{
	if (!is_valid_game_title_id(title_id))
	{
		return {};
	}
	return rpcs3::utils::get_custom_config_dir() + "presets/" + std::string{title_id} + "/";
}

const setting_record* find_setting(std::string_view key, setting_context context) noexcept
{
	for (const auto& setting : catalog)
	{
		if (setting.key == key && setting_is_available(setting.scope, context))
		{
			return &setting;
		}
	}
	return nullptr;
}

bool save_global_settings() noexcept
{
	return g_cfg.save(fs::get_config_dir(true) + "config.yml");
}

settings_load_error load_effective_settings(std::string_view title_id, bool& has_custom_config) noexcept
{
	has_custom_config = false;
	g_cfg.from_default();
	g_cfg.name.clear();

	const std::string global_path = fs::get_config_dir(true) + "config.yml";
	if (fs::file global_config{global_path})
	{
		if (!g_cfg.from_string(global_config.to_string()))
		{
			return settings_load_error::global_invalid;
		}
		g_cfg.name = global_path;
	}
	else
	{
		return settings_load_error::global_access_failed;
	}

	if (title_id.empty())
	{
		return settings_load_error::none;
	}

	const std::string custom_path = rpcs3::utils::get_custom_config_path(std::string{title_id});
	if (fs::file custom_config{custom_path})
	{
		has_custom_config = true;
		if (!g_cfg.from_string(custom_config.to_string()))
		{
			return settings_load_error::game_invalid;
		}
		g_cfg.name = custom_path;
		return settings_load_error::none;
	}

	return fs::g_tls_error == fs::error::noent
		? settings_load_error::none
		: settings_load_error::game_access_failed;
}

bool save_game_settings(std::string_view title_id) noexcept
{
	if (title_id.empty() || !fs::create_path(rpcs3::utils::get_custom_config_dir()))
	{
		return false;
	}
	return g_cfg.save(rpcs3::utils::get_custom_config_path(std::string{title_id}));
}

bool remove_game_settings(std::string_view title_id) noexcept
{
	if (title_id.empty())
	{
		return false;
	}

	const std::string path = rpcs3::utils::get_custom_config_path(std::string{title_id});
	return fs::remove_file(path) || fs::g_tls_error == fs::error::noent;
}

std::string_view settings_load_error_detail(settings_load_error error) noexcept
{
	switch (error)
	{
	case settings_load_error::none:
		return {};
	case settings_load_error::global_access_failed:
		return "Unable to read RPCS3's global config.yml";
	case settings_load_error::global_invalid:
		return "RPCS3's global config.yml is invalid";
	case settings_load_error::game_access_failed:
		return "Unable to read the game's custom configuration";
	case settings_load_error::game_invalid:
		return "The game's custom configuration is invalid";
	}
	return "Unable to load RPCS3 settings";
}

game_settings_preset_result enumerate_game_settings_presets(
	std::string_view title_id,
	std::vector<game_settings_preset>& presets)
{
	if (title_id.empty())
	{
		return preset_failure(
			game_settings_preset_error::storage_failed,
			"A game title ID is required");
	}
	return read_preset_records(title_id, presets);
}

game_settings_preset_result save_current_game_settings_preset(
	std::string_view title_id,
	std::string_view name)
{
	if (auto result = validate_new_preset_name(title_id, name); !result)
	{
		return result;
	}
	const std::string content = g_cfg.to_string();
	if (content.empty() || content.size() > maximum_preset_config_size)
	{
		return preset_failure(
			game_settings_preset_error::invalid_config,
			"The active game settings cannot be represented as a bounded preset");
	}
	if (!fs::create_path(game_settings_preset_directory(title_id)))
	{
		return preset_failure(
			game_settings_preset_error::storage_failed,
			"RPCS3 could not create the game-settings preset directory");
	}
	return write_configuration(preset_path(title_id, name), content);
}

game_settings_preset_result apply_game_settings_preset(
	std::string_view title_id,
	std::string_view name)
{
	game_settings_preset preset;
	if (auto result = locate_preset(title_id, name, preset); !result)
	{
		return result;
	}
	std::string content;
	if (auto result = read_validated_configuration(preset_path(title_id, preset.name), content); !result)
	{
		return result;
	}
	if (!fs::create_path(rpcs3::utils::get_custom_config_dir()))
	{
		return preset_failure(
			game_settings_preset_error::storage_failed,
			"RPCS3 could not create its custom configuration directory");
	}
	return write_configuration(rpcs3::utils::get_custom_config_path(std::string{title_id}), content);
}

game_settings_preset_result duplicate_game_settings_preset(
	std::string_view title_id,
	std::string_view source_name,
	std::string_view destination_name)
{
	game_settings_preset source;
	if (auto result = locate_preset(title_id, source_name, source); !result)
	{
		return result;
	}
	if (auto result = validate_new_preset_name(title_id, destination_name); !result)
	{
		return result;
	}
	std::string content;
	if (auto result = read_validated_configuration(preset_path(title_id, source.name), content); !result)
	{
		return result;
	}
	return write_configuration(preset_path(title_id, destination_name), content);
}

game_settings_preset_result rename_game_settings_preset(
	std::string_view title_id,
	std::string_view source_name,
	std::string_view destination_name)
{
	game_settings_preset source;
	if (auto result = locate_preset(title_id, source_name, source); !result)
	{
		return result;
	}
	if (source.name == destination_name)
	{
		return {};
	}
	if (auto result = validate_new_preset_name(title_id, destination_name, source.name); !result)
	{
		return result;
	}
	if (!fs::rename(
		preset_path(title_id, source.name),
		preset_path(title_id, destination_name),
		false))
	{
		return preset_failure(
			game_settings_preset_error::storage_failed,
			"RPCS3 could not rename the settings preset");
	}
	return {};
}

game_settings_preset_result delete_game_settings_preset(
	std::string_view title_id,
	std::string_view name)
{
	game_settings_preset preset;
	if (auto result = locate_preset(title_id, name, preset); !result)
	{
		return result;
	}
	if (!fs::remove_file(preset_path(title_id, preset.name)))
	{
		return preset_failure(
			game_settings_preset_error::storage_failed,
			"RPCS3 could not delete the settings preset");
	}
	return {};
}

game_settings_preset_result import_game_settings_preset(
	std::string_view title_id,
	std::string_view source_path,
	std::string_view name)
{
	if (auto result = validate_new_preset_name(title_id, name); !result)
	{
		return result;
	}
	std::string content;
	if (auto result = read_validated_configuration(source_path, content); !result)
	{
		return result;
	}
	if (!fs::create_path(game_settings_preset_directory(title_id)))
	{
		return preset_failure(
			game_settings_preset_error::storage_failed,
			"RPCS3 could not create the game-settings preset directory");
	}
	return write_configuration(preset_path(title_id, name), content);
}

game_settings_preset_result export_game_settings_preset(
	std::string_view title_id,
	std::string_view name,
	std::string_view destination_path)
{
	game_settings_preset preset;
	if (auto result = locate_preset(title_id, name, preset); !result)
	{
		return result;
	}
	std::string content;
	if (auto result = read_validated_configuration(preset_path(title_id, preset.name), content); !result)
	{
		return result;
	}
	return write_configuration(destination_path, content);
}
}
