#include "RPCS3IOSRuntimePatches.h"

#include "Crypto/utils.h"
#include "Utilities/File.h"
#include "Utilities/bin_patch.h"

#include <algorithm>
#include <sstream>
#include <tuple>

namespace rpcs3::ios
{
namespace
{
constexpr std::string_view repository_base_url =
	"https://rpcs3.net/compatibility?patch&api=v1&v=";

bool has_suffix(std::string_view value, std::string_view suffix)
{
	return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
}

bool load_all_patch_files(patch_engine::patch_map& patches, std::string& detail)
{
	const std::string directory = patch_engine::get_patches_path();
	std::vector<std::string> names{"patch.yml", "imported_patch.yml"};

	if (fs::is_dir(directory))
	{
		for (const auto& entry : fs::dir{directory})
		{
			if (!entry.is_directory && !entry.is_symlink && has_suffix(entry.name, "_patch.yml"))
			{
				names.emplace_back(entry.name);
			}
		}
	}

	std::sort(names.begin() + 2, names.end());
	names.erase(std::unique(names.begin(), names.end()), names.end());

	for (const std::string& name : names)
	{
		std::stringstream messages;
		if (!patch_engine::load(patches, directory + name, "", false, &messages))
		{
			detail = messages.str();
			if (detail.empty())
			{
				detail = "RPCS3 could not parse " + name;
			}
			return false;
		}
	}

	return true;
}

patch_engine::patch_config_values* find_config_values(
	patch_engine::patch_info& patch,
	std::string_view title,
	std::string_view serial,
	std::string_view app_version)
{
	auto title_it = patch.titles.find(std::string{title});
	if (title_it == patch.titles.end())
	{
		return nullptr;
	}
	auto serial_it = title_it->second.find(std::string{serial});
	if (serial_it == title_it->second.end())
	{
		return nullptr;
	}
	auto version_it = serial_it->second.find(std::string{app_version});
	return version_it == serial_it->second.end() ? nullptr : &version_it->second;
}
}

std::string patch_repository_url()
{
	std::string url = std::string{repository_base_url} + patch_engine_version;
	const std::string path = patch_engine::get_patches_path() + "patch.yml";

	if (fs::is_file(path))
	{
		if (fs::file file{path})
		{
			const std::string content = file.to_string();
			url += "&sha256=" + sha256_get_hash(content.data(), content.size(), true);
		}
	}

	return url;
}

patch_repository_install_result install_patch_repository(
	std::string_view version,
	std::string_view expected_sha256,
	std::string_view content)
{
	if (version != patch_engine_version)
	{
		return {patch_repository_install_error::invalid_repository,
			"The patch repository targets Patch Engine " + std::string{version} +
			", but this core requires " + patch_engine_version};
	}
	if (content.empty() || expected_sha256.size() != 64)
	{
		return {patch_repository_install_error::invalid_repository,
			"The patch repository response is missing YAML content or a SHA-256 checksum"};
	}
	if (sha256_get_hash(content.data(), content.size(), true) != expected_sha256)
	{
		return {patch_repository_install_error::invalid_repository,
			"The downloaded patch repository does not match its SHA-256 checksum"};
	}

	patch_engine::patch_map validated;
	std::stringstream messages;
	if (!patch_engine::load(validated, "Downloaded patch repository", std::string{content}, true, &messages))
	{
		std::string detail = messages.str();
		if (detail.empty())
		{
			detail = "RPCS3 rejected the downloaded Patch Engine YAML";
		}
		return {patch_repository_install_error::invalid_repository, std::move(detail)};
	}

	const std::string directory = patch_engine::get_patches_path();
	if (!fs::create_path(directory))
	{
		return {patch_repository_install_error::storage_failed,
			"RPCS3 could not create its patches directory"};
	}

	const std::string path = directory + "patch.yml";
	if (fs::is_file(path) && !fs::copy_file(path, path + ".old", true))
	{
		return {patch_repository_install_error::storage_failed,
			"RPCS3 could not back up the current patch repository"};
	}

	fs::pending_file pending{path};
	if (!pending.file || pending.file.write(content.data(), content.size()) != content.size() || !pending.commit())
	{
		return {patch_repository_install_error::storage_failed,
			"RPCS3 could not atomically save the downloaded patch repository"};
	}

	return {};
}

runtime_patch_list_result runtime_patches_for_title(
	std::string_view title_id,
	std::string_view installed_version)
{
	patch_engine::patch_map patches;
	runtime_patch_list_result result;
	if (!load_all_patch_files(patches, result.detail))
	{
		result.error = runtime_patch_update_error::invalid_patch_files;
		return result;
	}

	for (const auto& [hash, container] : patches)
	{
		for (const auto& [description, patch] : container.patch_info_map)
		{
			for (const auto& [title, serials] : patch.titles)
			{
				auto serial_it = serials.find(std::string{title_id});
				if (serial_it == serials.end())
				{
					continue;
				}

				for (const auto& [app_version, config] : serial_it->second)
				{
					if (app_version != patch_key::all && app_version != installed_version)
					{
						continue;
					}

					result.patches.push_back(runtime_patch_info{
						hash,
						title,
						description,
						patch.patch_version,
						patch.author,
						patch.notes,
						patch.patch_group,
						app_version,
						config.enabled,
						static_cast<std::uint32_t>(patch.default_config_values.size()),
					});
				}
			}
		}
	}

	std::sort(result.patches.begin(), result.patches.end(), [](const auto& left, const auto& right)
	{
		return std::tie(left.title, left.description, left.app_version, left.hash) <
			std::tie(right.title, right.description, right.app_version, right.hash);
	});
	return result;
}

runtime_patch_update_result set_runtime_patch_enabled(
	std::string_view title_id,
	std::string_view hash,
	std::string_view title,
	std::string_view app_version,
	std::string_view description,
	bool enabled)
{
	patch_engine::patch_map patches;
	runtime_patch_update_result result;
	if (!load_all_patch_files(patches, result.detail))
	{
		result.error = runtime_patch_update_error::invalid_patch_files;
		return result;
	}

	auto container_it = patches.find(std::string{hash});
	if (container_it == patches.end())
	{
		return {runtime_patch_update_error::patch_not_found, "The selected game patch no longer exists"};
	}
	auto patch_it = container_it->second.patch_info_map.find(std::string{description});
	if (patch_it == container_it->second.patch_info_map.end())
	{
		return {runtime_patch_update_error::patch_not_found, "The selected game patch no longer exists"};
	}
	auto* config = find_config_values(patch_it->second, title, title_id, app_version);
	if (!config)
	{
		return {runtime_patch_update_error::patch_not_found,
			"The selected game patch does not target this title and application version"};
	}

	config->enabled = enabled;
	const std::string group = patch_it->second.patch_group;
	if (enabled && !group.empty())
	{
		for (auto& [other_hash, other_container] : patches)
		{
			for (auto& [other_description, other_patch] : other_container.patch_info_map)
			{
				if (other_patch.patch_group != group ||
					(other_hash == hash && other_description == description))
				{
					continue;
				}
				if (auto* other = find_config_values(other_patch, title, title_id, app_version))
				{
					other->enabled = false;
				}
			}
		}
	}

	if (!patch_engine::save_config(patches))
	{
		return {runtime_patch_update_error::storage_failed,
			"RPCS3 could not save the game-patch configuration"};
	}
	return {};
}
}
