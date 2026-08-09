#include "GameLibrary.h"

#include "Utilities/StrFmt.h"
#include "Crypto/unpkg.h"
#include "Emu/system_utils.hpp"
#include "Emu/vfs_config.h"
#include "Loader/ISO.h"
#include "Loader/PSF.h"
#include "Utilities/File.h"
#include "Utilities/Thread.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <deque>
#include <limits>
#include <thread>
#include <vector>

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

game_iso_install_result invalid_iso(std::string detail)
{
	return {game_iso_install_error::invalid_iso, {}, {}, std::move(detail)};
}

game_iso_install_result iso_installation_failed(
	std::string detail,
	std::string title_id = {},
	std::string title = {})
{
	return {
		game_iso_install_error::installation_failed,
		std::move(title_id),
		std::move(title),
		std::move(detail),
	};
}

bool valid_title_id(std::string_view title_id)
{
	return !title_id.empty() && title_id.size() <= 32 &&
		std::all_of(title_id.begin(), title_id.end(), [](unsigned char character)
		{
			return std::isalnum(character) || character == '_' || character == '-';
		});
}

std::string disc_image_root()
{
	return rpcs3::utils::get_games_dir() + "DiscImages/";
}

std::string formatted_byte_size(u64 bytes)
{
	constexpr double gib = 1024.0 * 1024.0 * 1024.0;
	constexpr double mib = 1024.0 * 1024.0;
	if (bytes >= static_cast<u64>(gib))
	{
		return fmt::format("%.2f GiB", bytes / gib);
	}
	if (bytes >= static_cast<u64>(mib))
	{
		return fmt::format("%.1f MiB", bytes / mib);
	}
	return fmt::format("%u bytes", bytes);
}

struct temporary_directory
{
	std::string path;

	~temporary_directory()
	{
		if (!path.empty())
		{
			fs::remove_all(path, true, true);
		}
	}
};

bool copy_file_with_progress(
	const std::string& source_path,
	const std::string& destination_path,
	u64& copied,
	u64 total,
	std::string_view stage,
	const game_iso_progress_callback& progress)
{
	fs::file source{source_path, fs::read};
	fs::file destination{destination_path, fs::rewrite};
	if (!source || !destination)
	{
		return false;
	}

	std::vector<u8> buffer(4 * 1024 * 1024);
	while (copied < total)
	{
		const u64 count = source.read(buffer.data(), buffer.size());
		if (!count)
		{
			break;
		}
		if (destination.write(buffer.data(), count) != count)
		{
			return false;
		}

		copied += count;
		if (progress)
		{
			const u32 scaled = total
				? static_cast<u32>(std::min<u64>(900, copied * 900 / total))
				: 900;
			progress(scaled, 1000, fmt::format("%s (%s of %s)", stage,
				formatted_byte_size(copied), formatted_byte_size(total)));
		}
	}

	return source.pos() == source.size();
}

bool extract_iso_file(iso_archive& archive, const std::string& source, const std::string& destination)
{
	if (!archive.is_file(source))
	{
		return false;
	}

	fs::file input{archive.open(source)};
	fs::file output{destination, fs::rewrite};
	if (!input || !output)
	{
		return false;
	}

	std::vector<u8> buffer(256 * 1024);
	for (;;)
	{
		const u64 count = input.read(buffer.data(), buffer.size());
		if (!count)
		{
			return input.pos() == input.size();
		}
		if (output.write(buffer.data(), count) != count)
		{
			return false;
		}
	}
}

bool has_iso_descriptor_terminator(const std::string& path)
{
	fs::file file{path, fs::read};
	if (!file)
	{
		return false;
	}

	std::array<u8, 6> descriptor{};
	for (u64 index = 0; index < 256; index++)
	{
		const u64 offset = (16 + index) * ISO_SECTOR_SIZE;
		if (file.read_at(offset, descriptor.data(), descriptor.size()) != descriptor.size() ||
			descriptor[1] != 'C' || descriptor[2] != 'D' || descriptor[3] != '0' ||
			descriptor[4] != '0' || descriptor[5] != '1')
		{
			return false;
		}
		if (descriptor[0] == 255)
		{
			return true;
		}
	}
	return false;
}

std::optional<installed_game> installed_iso(const std::string& directory)
{
	const std::string iso_path = directory + "/disc.iso";
	const std::string sfo_path = directory + "/PARAM.SFO";
	if (!fs::is_file(iso_path) || !fs::is_file(sfo_path))
	{
		return std::nullopt;
	}

	const psf::registry metadata = psf::load_object(sfo_path);
	std::string title_id{psf::get_string(metadata, "TITLE_ID")};
	std::string category{psf::get_string(metadata, "CATEGORY")};
	if (!valid_title_id(title_id) || category != "DG")
	{
		return std::nullopt;
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
	std::string icon_path = directory + "/ICON0.PNG";
	if (!fs::is_file(icon_path))
	{
		icon_path.clear();
	}

	return installed_game{
		std::move(title_id),
		std::move(title),
		std::move(version),
		std::move(category),
		iso_path,
		std::move(icon_path),
	};
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

game_iso_install_result install_game_iso(
	const std::string& iso_path,
	const std::string& key_path,
	const game_iso_progress_callback& progress)
{
	if (iso_path.empty() || iso_path.front() != '/' ||
		(!key_path.empty() && key_path.front() != '/'))
	{
		return invalid_iso("ISO and key paths must be absolute sandbox paths");
	}

	u64 iso_size = 0;
	if (!is_iso_file(iso_path, &iso_size) || !iso_size)
	{
		return invalid_iso("The selected file is not a valid ISO 9660 disc image");
	}

	u64 key_size = 0;
	if (!key_path.empty())
	{
		fs::file key{key_path, fs::read};
		if (!key || !(key_size = key.size()))
		{
			return invalid_iso("The selected disc key could not be read");
		}
	}
	if (iso_size > std::numeric_limits<u64>::max() - key_size)
	{
		return iso_installation_failed("The selected ISO size is invalid");
	}
	const u64 total_size = iso_size + key_size;

	const std::string root = disc_image_root();
	if (!fs::create_path(root))
	{
		return iso_installation_failed("Unable to create the disc-image library");
	}
	for (auto&& entry : fs::dir{root})
	{
		if (entry.is_directory && entry.name.starts_with(".import-"))
		{
			fs::remove_all(root + entry.name, true, true);
		}
	}
	fs::device_stat device{};
	if (!fs::statfs(root, device))
	{
		return iso_installation_failed("Unable to determine free space for ISO installation");
	}
	if (device.avail_free < total_size)
	{
		return iso_installation_failed(fmt::format(
			"Not enough free space to install the ISO (need at least %s more)",
			formatted_byte_size(total_size - device.avail_free)));
	}
	static std::atomic<u64> import_counter{0};
	temporary_directory temporary{
		root + fmt::format(".import-%x", import_counter.fetch_add(1, std::memory_order_relaxed))
	};
	if (!fs::create_dir(temporary.path))
	{
		return iso_installation_failed("Unable to create temporary ISO installation storage");
	}

	if (progress)
	{
		progress(0, 1000, "Copying disc image into RPCS3 storage");
	}
	u64 copied = 0;
	const std::string temporary_iso = temporary.path + "/disc.iso";
	if (!copy_file_with_progress(iso_path, temporary_iso, copied, total_size,
		"Copying disc image", progress))
	{
		return iso_installation_failed("Unable to copy the selected ISO into RPCS3 storage");
	}
	if (!key_path.empty() && !copy_file_with_progress(key_path, temporary.path + "/disc.dkey",
		copied, total_size, "Copying disc key", progress))
	{
		return iso_installation_failed("Unable to copy the selected disc key into RPCS3 storage");
	}

	if (progress)
	{
		progress(920, 1000, "Validating PlayStation 3 disc metadata");
	}
	if (!has_iso_descriptor_terminator(temporary_iso))
	{
		return invalid_iso("The selected image has an invalid or unterminated ISO descriptor sequence");
	}
	iso_archive archive{temporary_iso};
	const psf::registry metadata = archive.open_psf("PS3_GAME/PARAM.SFO");
	std::string title_id{psf::get_string(metadata, "TITLE_ID")};
	std::string title{psf::get_string(metadata, "TITLE")};
	const std::string category{psf::get_string(metadata, "CATEGORY")};
	if (!valid_title_id(title_id) || category != "DG" ||
		!archive.is_file("PS3_GAME/USRDIR/EBOOT.BIN"))
	{
		return invalid_iso("The selected image is not a bootable PlayStation 3 game ISO. Encrypted Redump images require their matching .dkey or .key file");
	}
	if (title.empty())
	{
		title = title_id;
	}

	if (!extract_iso_file(archive, "PS3_GAME/PARAM.SFO", temporary.path + "/PARAM.SFO"))
	{
		return iso_installation_failed("Unable to preserve the ISO metadata", title_id, title);
	}
	if (archive.is_file("PS3_GAME/ICON0.PNG") &&
		!extract_iso_file(archive, "PS3_GAME/ICON0.PNG", temporary.path + "/ICON0.PNG"))
	{
		return iso_installation_failed("Unable to preserve the ISO artwork", title_id, title);
	}

	const std::string final_directory = root + title_id;
	if (fs::is_dir(final_directory))
	{
		return iso_installation_failed(
			fmt::format("A disc image for %s is already installed", title_id), title_id, title);
	}
	if (!fs::rename(temporary.path, final_directory, false))
	{
		return iso_installation_failed("Unable to finalize the ISO installation", title_id, title);
	}
	temporary.path.clear();

	if (progress)
	{
		progress(1000, 1000, fmt::format("Installed %s", title));
	}
	return {game_iso_install_error::none, std::move(title_id), std::move(title), {}};
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

	const std::string iso_root = disc_image_root();
	for (auto&& entry : fs::dir{iso_root})
	{
		if (!entry.is_directory || entry.name == "." || entry.name == ".." ||
			entry.name.starts_with(".import-"))
		{
			continue;
		}
		if (auto game = installed_iso(iso_root + entry.name))
		{
			result.emplace_back(std::move(*game));
		}
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
