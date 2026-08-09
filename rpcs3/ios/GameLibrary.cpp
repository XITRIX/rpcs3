#include "GameLibrary.h"

#include "Utilities/StrFmt.h"
#include "Crypto/unpkg.h"
#include "Emu/system_utils.hpp"
#include "Emu/vfs_config.h"
#include "Loader/PSF.h"
#include "Utilities/File.h"
#include "Utilities/Thread.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <thread>

namespace rpcs3::ios
{
namespace
{
game_package_install_result invalid_package(std::string detail)
{
	return {game_package_install_error::invalid_package, {}, {}, std::move(detail)};
}

game_package_install_result installation_failed(
	std::string detail,
	std::string title_id = {},
	std::string title = {})
{
	return {
		game_package_install_error::installation_failed,
		std::move(title_id),
		std::move(title),
		std::move(detail),
	};
}

std::string package_failure_detail(const package_install_result& result)
{
	if (result.error != package_install_result::error_type::app_version)
	{
		return "RPCS3 could not extract the selected package";
	}

	std::string detail = "RPCS3 rejected the package because its application version is incompatible";
	if (!result.version.app_ver.empty())
	{
		fmt::append(detail, " (package %s", result.version.app_ver);
		if (!result.version.expected.empty())
		{
			fmt::append(detail, ", expected %s", result.version.expected);
		}
		if (!result.version.installed.empty())
		{
			fmt::append(detail, ", installed %s", result.version.installed);
		}
		detail += ')';
	}
	return detail;
}

bool has_bootable_file(const std::string& path)
{
	return fs::is_file(path + "/EBOOT.BIN") ||
		fs::is_file(path + "/USRDIR/EBOOT.BIN") ||
		fs::is_file(path + "/USRDIR/ISO.BIN.EDAT");
}
}

game_package_install_result install_game_package(
	const std::string& path,
	const game_package_progress_callback& progress)
{
	if (path.empty() || path.front() != '/')
	{
		return invalid_package("The package path must be an absolute sandbox path");
	}

	std::deque<package_reader> readers;
	readers.emplace_back(path);
	package_reader& reader = readers.front();
	if (!reader.is_valid())
	{
		return invalid_package("The selected file is not a valid PlayStation package");
	}
	if (reader.get_header().pkg_platform != PKG_PLATFORM_TYPE_PS3)
	{
		return invalid_package("The selected package is not for PlayStation 3");
	}

	const psf::registry& package_psf = reader.get_psf();
	const std::string title_id{psf::get_string(package_psf, "TITLE_ID")};
	std::string title{psf::get_string(package_psf, "TITLE")};
	if (title.empty())
	{
		title = title_id.empty() ? "PlayStation 3 package" : title_id;
	}

	fs::device_stat device{};
	const std::string hdd0_path = rpcs3::utils::get_hdd0_dir();
	const u64 required_size = reader.get_header().data_size;
	if (!fs::statfs(hdd0_path, device))
	{
		return installation_failed(
			"Unable to determine free space for package installation", title_id, title);
	}
	if (device.avail_free < required_size)
	{
		return installation_failed(fmt::format(
			"Not enough free space to install the package (need at least %u more bytes)",
			required_size - device.avail_free), title_id, title);
	}

	if (progress)
	{
		progress(0, 1000, fmt::format("Preparing %s", title));
	}

	package_install_result extraction_result{};
	std::deque<std::string> bootable_paths;
	named_thread worker("iOS PKG Installer", [&readers, &extraction_result, &bootable_paths]
	{
		extraction_result = package_reader::extract_data(readers, bootable_paths);
		return extraction_result.error == package_install_result::error_type::no_error;
	});

	int last_progress = -1;
	while (std::this_thread::sleep_for(std::chrono::milliseconds(50)),
		worker <= thread_state::aborting)
	{
		const int current = reader.get_progress(1000);
		if (progress && current != last_progress)
		{
			last_progress = current;
			progress(static_cast<u32>(std::max(current, 0)), 1000,
				fmt::format("Installing %s", title));
		}
	}

	const bool succeeded = worker();
	if (!succeeded || extraction_result.error != package_install_result::error_type::no_error ||
		reader.get_result() != package_reader::result::success)
	{
		return installation_failed(package_failure_detail(extraction_result), title_id, title);
	}

	if (progress)
	{
		progress(1000, 1000, fmt::format("Installed %s", title));
	}
	return {game_package_install_error::none, title_id, title, {}};
}

std::vector<installed_game> installed_games()
{
	std::vector<installed_game> result;
	const std::string game_root = rpcs3::utils::get_hdd0_dir() + "game/";

	for (auto&& entry : fs::dir{game_root})
	{
		if (!entry.is_directory || entry.name == "." || entry.name == ".." ||
			entry.name == "＄locks")
		{
			continue;
		}

		const std::string path = game_root + entry.name;
		const std::string sfo_path = path + "/PARAM.SFO";
		if (!fs::is_file(sfo_path) || !has_bootable_file(path))
		{
			continue;
		}

		const psf::registry metadata = psf::load_object(sfo_path);
		const std::string title_id{psf::get_string(metadata, "TITLE_ID")};
		const std::string category{psf::get_string(metadata, "CATEGORY")};
		if (title_id.empty() || !psf::is_cat_hdd(category))
		{
			continue;
		}

		std::string title{psf::get_string(metadata, "TITLE")};
		if (title.empty())
		{
			title = title_id;
		}
		std::string version{psf::get_string(metadata, "APP_VER")};
		if (version.empty())
		{
			version = std::string{psf::get_string(metadata, "VERSION")};
		}

		std::string icon_path = path + "/ICON0.PNG";
		if (!fs::is_file(icon_path))
		{
			icon_path.clear();
		}

		result.push_back({
			std::move(title_id),
			std::move(title),
			std::move(version),
			std::move(category),
			path,
			std::move(icon_path),
		});
	}

	std::sort(result.begin(), result.end(), [](const installed_game& left, const installed_game& right)
	{
		return left.title_id < right.title_id;
	});
	result.erase(std::unique(result.begin(), result.end(), [](const installed_game& left, const installed_game& right)
	{
		return left.title_id == right.title_id;
	}), result.end());
	std::sort(result.begin(), result.end(), [](const installed_game& left, const installed_game& right)
	{
		return left.title == right.title
			? left.title_id < right.title_id
			: left.title < right.title;
	});
	return result;
}

std::optional<installed_game> find_installed_game(std::string_view title_id)
{
	std::vector<installed_game> games = installed_games();
	const auto found = std::find_if(games.begin(), games.end(), [title_id](const installed_game& game)
	{
		return game.title_id == title_id;
	});
	if (found == games.end())
	{
		return std::nullopt;
	}
	return std::move(*found);
}
}
