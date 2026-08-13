#pragma once

#include "RPCS3IOS.h"
#include "RPCS3IOSSettingScope.h"

#include <span>
#include <string>
#include <string_view>

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

std::span<const setting_record> settings_catalog() noexcept;
const setting_record* find_setting(std::string_view key, setting_context context) noexcept;
bool save_global_settings() noexcept;
settings_load_error load_effective_settings(std::string_view title_id, bool& has_custom_config) noexcept;
bool save_game_settings(std::string_view title_id) noexcept;
bool remove_game_settings(std::string_view title_id) noexcept;
std::string_view settings_load_error_detail(settings_load_error error) noexcept;
}
