#pragma once

#include "util/types.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rpcs3::ios
{
enum class game_package_install_error
{
	none,
	invalid_package,
	installation_failed,
};

enum class game_iso_install_error
{
	none,
	invalid_iso,
	installation_failed,
};

enum class game_zip_install_error
{
	none,
	invalid_zip,
	installation_failed,
};

enum class game_folder_install_error
{
	none,
	invalid_folder,
	installation_failed,
};

enum class game_patch_install_error
{
	none,
	invalid_patch,
	title_mismatch,
	installation_failed,
};

enum class game_delete_error
{
	none,
	not_found,
	deletion_failed,
};

struct game_package_install_result
{
	game_package_install_error error = game_package_install_error::none;
	std::string title_id;
	std::string title;
	std::string detail;
};

struct game_iso_install_result
{
	game_iso_install_error error = game_iso_install_error::none;
	std::string title_id;
	std::string title;
	std::string detail;
};

struct game_zip_install_result
{
	game_zip_install_error error = game_zip_install_error::none;
	std::string title_id;
	std::string title;
	std::string detail;
};

struct game_folder_install_result
{
	game_folder_install_error error = game_folder_install_error::none;
	std::string title_id;
	std::string title;
	std::string detail;
};

struct game_patch_install_result
{
	game_patch_install_error error = game_patch_install_error::none;
	std::string title_id;
	std::string title;
	std::string version;
	std::string detail;
};

struct game_delete_result
{
	game_delete_error error = game_delete_error::none;
	std::string title_id;
	std::string title;
	std::string detail;
};

struct installed_game
{
	std::string title_id;
	std::string title;
	std::string version;
	std::string category;
	u32 resolution = 0;
	std::string path;
	std::string icon_path;
	std::string firmware_version;
	u32 attribute = 0;
	u32 bootable = 0;
	u32 parental_level = 0;
	u32 sound_format = 0;
	u64 size_on_disk = umax;
};

struct installed_game_patch
{
	std::string title_id;
	std::string title;
	std::string version;
};

using game_package_progress_callback =
	std::function<void(u32 completed, u32 total, std::string_view stage)>;
using game_iso_progress_callback =
	std::function<void(u32 completed, u32 total, std::string_view stage)>;
using game_zip_progress_callback =
	std::function<void(u32 completed, u32 total, std::string_view stage)>;
using game_folder_progress_callback =
	std::function<void(u32 completed, u32 total, std::string_view stage)>;

game_package_install_result install_game_package(
	const std::string& path,
	const game_package_progress_callback& progress);
game_iso_install_result install_game_iso(
	const std::string& iso_path,
	const std::string& key_path,
	const game_iso_progress_callback& progress);
game_zip_install_result install_game_zip(
	const std::string& zip_path,
	const game_zip_progress_callback& progress);
game_folder_install_result install_game_folder(
	const std::string& folder_path,
	const game_folder_progress_callback& progress);
game_patch_install_result install_game_patch(
	std::string_view expected_title_id,
	const std::string& package_path,
	const game_package_progress_callback& progress);
game_delete_result delete_installed_game(std::string_view title_id);
bool is_valid_game_title_id(std::string_view title_id) noexcept;
std::vector<installed_game> installed_games();
std::optional<installed_game> find_installed_game(std::string_view title_id);
std::vector<installed_game_patch> installed_game_patches(std::string_view title_id);
}
