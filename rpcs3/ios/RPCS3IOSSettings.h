#pragma once

#include "RPCS3IOS.h"

#include <span>
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
};

std::span<const setting_record> settings_catalog() noexcept;
const setting_record* find_setting(std::string_view key) noexcept;
bool save_global_settings() noexcept;
}
