#pragma once

#include "RPCS3IOS.h"
#include "RPCS3IOSSettingScope.h"

#include <span>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cfg
{
class _base;
}

namespace rpcs3::ios
{
struct setting_record
{
	std::string_view key;
	std::string_view category;
	std::string_view section;
	std::string_view name;
	std::string_view description;
	rpcs3_ios_setting_kind kind;
	cfg::_base* entry;
	double minimum;
	double maximum;
	double step;
	setting_scope scope;
};

enum class settings_load_error
{
	none,
	global_access_failed,
	global_invalid,
	game_access_failed,
	game_invalid,
};

struct game_settings_preset
{
	std::string name;
	std::uint64_t size = 0;
	std::int64_t modified_time = 0;
};

enum class game_settings_preset_error
{
	none,
	invalid_name,
	already_exists,
	not_found,
	invalid_config,
	too_many_presets,
	storage_failed,
};

struct game_settings_preset_result
{
	game_settings_preset_error error = game_settings_preset_error::none;
	std::string detail;

	explicit operator bool() const noexcept
	{
		return error == game_settings_preset_error::none;
	}
};

std::span<const setting_record> settings_catalog() noexcept;
const setting_record* find_setting(std::string_view key, setting_context context) noexcept;
bool save_global_settings() noexcept;
settings_load_error load_effective_settings(std::string_view title_id, bool& has_custom_config) noexcept;
bool save_game_settings(std::string_view title_id) noexcept;
bool remove_game_settings(std::string_view title_id) noexcept;
std::string game_settings_preset_directory(std::string_view title_id);
std::string_view settings_load_error_detail(settings_load_error error) noexcept;
game_settings_preset_result enumerate_game_settings_presets(
	std::string_view title_id,
	std::vector<game_settings_preset>& presets);
game_settings_preset_result save_current_game_settings_preset(
	std::string_view title_id,
	std::string_view name);
game_settings_preset_result apply_game_settings_preset(
	std::string_view title_id,
	std::string_view name);
game_settings_preset_result duplicate_game_settings_preset(
	std::string_view title_id,
	std::string_view source_name,
	std::string_view destination_name);
game_settings_preset_result rename_game_settings_preset(
	std::string_view title_id,
	std::string_view source_name,
	std::string_view destination_name);
game_settings_preset_result delete_game_settings_preset(
	std::string_view title_id,
	std::string_view name);
game_settings_preset_result import_game_settings_preset(
	std::string_view title_id,
	std::string_view source_path,
	std::string_view name);
game_settings_preset_result export_game_settings_preset(
	std::string_view title_id,
	std::string_view name,
	std::string_view destination_path);
}
