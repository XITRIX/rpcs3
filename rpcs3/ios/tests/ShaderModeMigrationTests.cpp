#include "Emu/system_config.h"
#include "Utilities/Thread.h"
#include "util/sysinfo.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>

// Use the actual production member type, registered in a small native cfg tree.
// Config.cpp supplies YAML loading, validation, overlay and serialization.
struct test_config : cfg::node
{
	struct video_node : cfg::node
	{
		video_node(cfg::node* owner) : cfg::node(owner, "Video") {}
		decltype(cfg_root::node_video::shadermode) shader{this, "Shader Mode", cfg_root::node_video::default_shader_mode};
		cfg::_int<25, 800> scale{this, "Resolution Scale", 100};
		cfg::_bool vsync{this, "VSync", false};
	} video{this};
	cfg::string marker{this, "Marker", "unchanged"};
};

// No emulator threads or logging transport are needed for configuration tests.
void logs::message::broadcast(const char*, const fmt_type_info*, ...) const {}
logs::registerer::registerer(logs::channel&) {}
u64 utils::_get_main_tid() { return 1; }
[[noreturn]] void thread_ctrl::emergency_exit(std::string_view) { std::abort(); }

#include "LogLevelFormatter.inc"

int main()
{
	constexpr auto replacement = shader_mode::async_with_interpreter;
#ifdef RPCS3_IOS
	constexpr auto legacy_result = replacement;
#else
	constexpr auto legacy_result = shader_mode::recompiler;
#endif
	unsigned cases = 0;
	for (const auto value : {"Legacy Recompiler (single-threaded)", "0", "0x0"})
	{
		test_config config;
		const std::string content = std::string{"Video:\n  Shader Mode: "} + value +
			"\n  Resolution Scale: 150\n  VSync: true\nMarker: preserved\n";
		assert(config.from_string(content));
		assert(config.video.shader == legacy_result);
		assert(config.video.scale == 150 && config.video.vsync.get());
		assert(config.marker.to_string() == "preserved");
		test_config reloaded;
		assert(reloaded.validate(config.to_string()));
		assert(reloaded.video.shader == legacy_result);
		assert(reloaded.video.scale == 150 && reloaded.video.vsync.get());
		assert(reloaded.marker.to_string() == "preserved");
#ifdef RPCS3_IOS
		assert(config.to_string().find("Legacy Recompiler") == std::string::npos);
#endif
		// Preset validation and applying the original bytes both use native parsing.
		test_config preset;
		assert(preset.validate(content));
		assert(preset.video.shader == legacy_result);
		++cases;
	}

	test_config config;
	assert(config.video.shader == replacement);
	const std::pair<const char*, shader_mode> retained[] = {
		{"Async Recompiler (multi-threaded)", shader_mode::async_recompiler},
		{"Async Recompiler with Shader Interpreter", replacement},
		{"Shader Interpreter only", shader_mode::interpreter_only},
		{"1", shader_mode::async_recompiler}, {"2", replacement}, {"3", shader_mode::interpreter_only},
	};
	for (const auto& [value, expected] : retained)
	{
		assert(config.video.shader.from_string(value));
		assert(config.video.shader == expected);
		// An unrelated per-game override must retain the global shader selection.
		assert(config.from_string("Video:\n  Resolution Scale: 200\n"));
		assert(config.video.shader == expected);
		assert(config.video.scale == 200);
		++cases;
	}
	assert(config.video.shader.from_string("1"));
	// An old per-game Legacy override migrates even over an explicit global mode.
	assert(config.from_string("Video:\n  Shader Mode: Legacy Recompiler (single-threaded)\n"));
	assert(config.video.shader == legacy_result);
	// The bridge holds a base pointer; virtual dispatch must apply the same policy.
	cfg::_base* entry = &config.video.shader;
	assert(entry->from_string("0"));
	assert(config.video.shader == legacy_result);
	for (const auto value : {"invalid", "4", "-1"})
	{
		assert(!entry->from_string(value));
		assert(config.video.shader == legacy_result);
		++cases;
	}
	config.from_default();
	assert(config.video.shader == replacement);
	assert(config.video.scale == 100 && !config.video.vsync.get());
	assert(!config.validate("Video:\n  Shader Mode: invalid\n"));

	// Execute the actual option-filter body used for both settings catalogs.
	struct { std::string_view key = "gpu.shader_mode"; } setting;
	auto options = entry->to_list();
#include "SettingsOptionFilter.inc"
	assert((options == std::vector<std::string>{
		"Async Recompiler (multi-threaded)",
		"Async Recompiler with Shader Interpreter",
		"Shader Interpreter only"}));
	std::printf("Shader settings: %u migration/preservation cases, YAML round trips, defaults and UI choices passed\n", cases);
}
