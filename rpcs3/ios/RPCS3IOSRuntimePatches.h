#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rpcs3::ios
{
enum class patch_repository_install_error
{
	none,
	invalid_repository,
	storage_failed,
};

struct patch_repository_install_result
{
	patch_repository_install_error error = patch_repository_install_error::none;
	std::string detail;
};

struct runtime_patch_info
{
	std::string hash;
	std::string title;
	std::string description;
	std::string patch_version;
	std::string author;
	std::string notes;
	std::string patch_group;
	std::string app_version;
	bool enabled = false;
	std::uint32_t configurable_count = 0;
};

enum class runtime_patch_update_error
{
	none,
	invalid_patch_files,
	patch_not_found,
	storage_failed,
};

struct runtime_patch_list_result
{
	runtime_patch_update_error error = runtime_patch_update_error::none;
	std::string detail;
	std::vector<runtime_patch_info> patches;
};

struct runtime_patch_update_result
{
	runtime_patch_update_error error = runtime_patch_update_error::none;
	std::string detail;
};

std::string patch_repository_url();

patch_repository_install_result install_patch_repository(
	std::string_view version,
	std::string_view expected_sha256,
	std::string_view content);

runtime_patch_list_result runtime_patches_for_title(
	std::string_view title_id,
	std::string_view installed_version);

runtime_patch_update_result set_runtime_patch_enabled(
	std::string_view title_id,
	std::string_view hash,
	std::string_view title,
	std::string_view app_version,
	std::string_view description,
	bool enabled);
}
