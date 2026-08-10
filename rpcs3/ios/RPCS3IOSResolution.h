#pragma once

#include "Loader/PSF.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace rpcs3::ios
{
inline constexpr std::string_view default_game_resolution = "1280x720";

constexpr u32 game_resolution_flag(std::string_view value)
{
	if (value == "720x480" || value == "720x480i")
	{
		return psf::resolution_flag::_480 | psf::resolution_flag::_480_16_9;
	}
	if (value == "720x576" || value == "720x576i")
	{
		return psf::resolution_flag::_576 | psf::resolution_flag::_576_16_9;
	}
	if (value == default_game_resolution)
	{
		return psf::resolution_flag::_720;
	}
	if (value == "1920x1080" || value == "1920x1080i" ||
		value == "1600x1080" || value == "1440x1080" ||
		value == "1280x1080" || value == "960x1080")
	{
		return psf::resolution_flag::_1080;
	}
	return 0;
}

constexpr bool game_supports_resolution(u32 resolution_flags, std::string_view value)
{
	if (!resolution_flags)
	{
		return true;
	}
	const u32 required_flag = game_resolution_flag(value);
	return required_flag && (resolution_flags & required_flag);
}

inline void filter_game_resolution_options(std::vector<std::string>& options, u32 resolution_flags)
{
	const bool has_default = std::ranges::find(options, default_game_resolution) != options.end();
	std::erase_if(options, [resolution_flags](const std::string& option)
	{
		return option.ends_with('i') || !game_supports_resolution(resolution_flags, option);
	});

	// Match Qt's fallback when the title exposes no usable progressive mode.
	if (resolution_flags && options.empty() && has_default)
	{
		options.emplace_back(default_game_resolution);
	}
}
}
