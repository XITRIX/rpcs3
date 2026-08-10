#pragma once

#include <string_view>

namespace rpcs3::ios
{
enum class game_patch_validation_error
{
	none,
	invalid_patch,
	title_mismatch,
};

inline game_patch_validation_error validate_game_patch_contract(
	std::string_view expected_title_id,
	std::string_view package_title_id,
	std::string_view category,
	bool has_patch_flag)
{
	if (expected_title_id.empty() || package_title_id.empty() ||
		category != "GD" || !has_patch_flag)
	{
		return game_patch_validation_error::invalid_patch;
	}
	return expected_title_id == package_title_id
		? game_patch_validation_error::none
		: game_patch_validation_error::title_mismatch;
}
}
