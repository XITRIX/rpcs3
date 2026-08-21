#include "GameLibrary.h"
#include "RPCS3IOSSettings.h"
#include "GameArchiveContract.h"
#include "GameFolderContract.h"
#include "GamePatchContract.h"

#include "Utilities/StrFmt.h"
#include "Crypto/unpkg.h"
#include "Emu/system_utils.hpp"
#include "Emu/vfs_config.h"
#include "Loader/ISO.h"
#include "Loader/PSF.h"
#include "Utilities/File.h"
#include "Utilities/Thread.h"

#include <minizip/unzip.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cerrno>
#include <deque>
#include <dirent.h>
#include <limits>
#include <sys/stat.h>
#include <thread>
#include <unordered_set>
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

game_zip_install_result invalid_zip(std::string detail)
{
	return {game_zip_install_error::invalid_zip, {}, {}, std::move(detail)};
}

game_zip_install_result zip_installation_failed(
	std::string detail,
	std::string title_id = {},
	std::string title = {})
{
	return {
		game_zip_install_error::installation_failed,
		std::move(title_id),
		std::move(title),
		std::move(detail),
	};
}

game_folder_install_result invalid_folder(std::string detail)
{
	return {game_folder_install_error::invalid_folder, {}, {}, std::move(detail)};
}

game_folder_install_result folder_installation_failed(
	std::string detail,
	std::string title_id = {},
	std::string title = {})
{
	return {
		game_folder_install_error::installation_failed,
		std::move(title_id),
		std::move(title),
		std::move(detail),
	};
}

game_patch_install_result invalid_patch(
	std::string detail,
	std::string title_id = {},
	std::string title = {},
	std::string version = {})
{
	return {
		game_patch_install_error::invalid_patch,
		std::move(title_id),
		std::move(title),
		std::move(version),
		std::move(detail),
	};
}

game_patch_install_result patch_title_mismatch(
	std::string detail,
	std::string title_id,
	std::string title,
	std::string version)
{
	return {
		game_patch_install_error::title_mismatch,
		std::move(title_id),
		std::move(title),
		std::move(version),
		std::move(detail),
	};
}

game_patch_install_result patch_installation_failed(
	std::string detail,
	std::string title_id = {},
	std::string title = {},
	std::string version = {})
{
	return {
		game_patch_install_error::installation_failed,
		std::move(title_id),
		std::move(title),
		std::move(version),
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

bool remove_installed_path(const std::string& path, std::string& detail)
{
	fs::stat_t info{};
	if (!fs::get_stat(path, info))
	{
		if (fs::g_tls_error == fs::error::noent)
		{
			return true;
		}
		detail = fmt::format("RPCS3 could not inspect %s", path);
		return false;
	}

	const bool removed = info.is_directory
		? fs::remove_all(path, true, false)
		: fs::remove_file(path);
	if (!removed)
	{
		detail = fmt::format("RPCS3 could not remove %s", path);
	}
	return removed;
}

bool remove_installed_paths_with_prefix(
	const std::string& root,
	std::string_view prefix,
	std::string& detail)
{
	std::vector<std::string> paths;
	for (auto&& entry : fs::dir{root})
	{
		const bool title_scoped = entry.name == prefix ||
			(entry.name.starts_with(prefix) && entry.name.size() > prefix.size() &&
				entry.name[prefix.size()] == '_');
		if (entry.name != "." && entry.name != ".." && title_scoped)
		{
			paths.emplace_back(root + entry.name);
		}
	}

	for (const std::string& path : paths)
	{
		if (!remove_installed_path(path, detail))
		{
			return false;
		}
	}
	return true;
}

std::string disc_image_root()
{
	return rpcs3::utils::get_games_dir() + "DiscImages/";
}

std::string extracted_game_root()
{
	return rpcs3::utils::get_games_dir() + "ExtractedGames/";
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

struct zip_handle
{
	unzFile value = nullptr;

	~zip_handle()
	{
		if (value)
		{
			unzClose(value);
		}
	}
};

struct folder_directory_handle
{
	DIR* value = nullptr;

	~folder_directory_handle()
	{
		if (value)
		{
			::closedir(value);
		}
	}
};

struct folder_tree_entry
{
	std::string relative_path;
	u64 size = 0;
	bool is_directory = false;
};

bool enumerate_folder_directory(
	const std::string& source_root,
	std::string_view relative_directory,
	u32 depth,
	std::vector<folder_tree_entry>& entries,
	std::vector<std::string>& files,
	u64& total_size,
	std::string& detail)
{
	if (depth > 64)
	{
		detail = "The selected folder is nested too deeply";
		return false;
	}

	const std::string directory_path = relative_directory.empty()
		? source_root
		: source_root + "/" + std::string{relative_directory};
	folder_directory_handle directory{::opendir(directory_path.c_str())};
	if (!directory.value)
	{
		detail = fmt::format("RPCS3 could not read the directory %s",
			relative_directory.empty() ? "." : relative_directory);
		return false;
	}

	std::vector<std::string> names;
	errno = 0;
	while (dirent* item = ::readdir(directory.value))
	{
		std::string name{item->d_name};
		if (name != "." && name != "..")
		{
			names.emplace_back(std::move(name));
		}
	}
	if (errno)
	{
		detail = fmt::format("RPCS3 could not finish reading the directory %s",
			relative_directory.empty() ? "." : relative_directory);
		return false;
	}
	std::sort(names.begin(), names.end());

	for (const std::string& name : names)
	{
		const std::string relative_path = relative_directory.empty()
			? name
			: std::string{relative_directory} + "/" + name;
		if (relative_path.size() > 4096 || entries.size() >= 1'000'000)
		{
			detail = relative_path.size() > 4096
				? "The selected folder contains a path that is too long"
				: "The selected folder contains too many entries";
			return false;
		}

		const std::string source_path = source_root + "/" + relative_path;
		struct stat info{};
		if (::lstat(source_path.c_str(), &info) != 0)
		{
			detail = fmt::format("RPCS3 could not inspect %s", relative_path);
			return false;
		}
		if (S_ISLNK(info.st_mode))
		{
			detail = fmt::format("The selected folder contains an unsupported link: %s", relative_path);
			return false;
		}
		if (S_ISDIR(info.st_mode))
		{
			entries.push_back({relative_path, 0, true});
			if (!enumerate_folder_directory(source_root, relative_path, depth + 1,
				entries, files, total_size, detail))
			{
				return false;
			}
			continue;
		}
		if (!S_ISREG(info.st_mode) || info.st_size < 0)
		{
			detail = fmt::format("The selected folder contains an unsupported special file: %s", relative_path);
			return false;
		}

		const u64 size = static_cast<u64>(info.st_size);
		if (size > std::numeric_limits<u64>::max() - total_size)
		{
			detail = "The selected folder reports an invalid total size";
			return false;
		}
		total_size += size;
		entries.push_back({relative_path, size, false});
		files.emplace_back(relative_path);
	}
	return true;
}

struct zip_entry
{
	unz64_file_pos position{};
	unz_file_info64 info{};
	std::string path;
	bool is_directory = false;
	bool is_symlink = false;
	bool is_special = false;
};

bool enumerate_zip_entries(
	unzFile archive,
	std::vector<zip_entry>& entries,
	std::vector<std::string>& files,
	std::string& detail)
{
	unz_global_info64 global{};
	if (unzGetGlobalInfo64(archive, &global) != UNZ_OK || !global.number_entry)
	{
		detail = "The selected file is empty or is not a readable ZIP archive";
		return false;
	}
	if (global.number_entry > 1'000'000)
	{
		detail = "The selected ZIP contains too many entries";
		return false;
	}
	if (unzGoToFirstFile(archive) != UNZ_OK)
	{
		detail = "RPCS3 could not read the ZIP directory";
		return false;
	}

	std::unordered_set<std::string> seen_paths;
	entries.reserve(static_cast<usz>(global.number_entry));
	files.reserve(static_cast<usz>(global.number_entry));
	for (ZPOS64_T index = 0; index < global.number_entry; index++)
	{
		unz_file_info64 info{};
		if (unzGetCurrentFileInfo64(archive, &info, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK ||
			!info.size_filename || info.size_filename > 4096)
		{
			detail = "The ZIP contains an invalid file name";
			return false;
		}

		std::vector<char> name(info.size_filename + 1, '\0');
		if (unzGetCurrentFileInfo64(archive, &info, name.data(), name.size(),
			nullptr, 0, nullptr, 0) != UNZ_OK)
		{
			detail = "RPCS3 could not read a ZIP entry";
			return false;
		}

		const std::string raw_path{name.data(), info.size_filename};
		const u32 source_system = (info.version >> 8) & 0xff;
		const mode_t unix_mode = static_cast<mode_t>(info.external_fa >> 16);
		const mode_t unix_type = unix_mode & S_IFMT;
		const bool has_unix_type = source_system == 3 || source_system == 19;
		const bool is_symlink = has_unix_type && unix_type == S_IFLNK;
		const bool unix_directory = has_unix_type && unix_type == S_IFDIR;
		const bool unix_regular = !has_unix_type || !unix_type || unix_type == S_IFREG;
		const bool is_directory = raw_path.ends_with('/') || unix_directory ||
			(info.external_fa & 0x10) != 0;
		const bool is_special = has_unix_type && unix_type &&
			!unix_regular && !unix_directory && !is_symlink;
		const auto normalized = normalize_game_archive_path(raw_path, is_directory);
		if (!normalized)
		{
			detail = fmt::format("The ZIP contains an unsafe path: %s", raw_path);
			return false;
		}
		if (!seen_paths.emplace(*normalized).second)
		{
			detail = fmt::format("The ZIP contains duplicate entries for %s", *normalized);
			return false;
		}

		unz64_file_pos position{};
		if (unzGetFilePos64(archive, &position) != UNZ_OK)
		{
			detail = "RPCS3 could not index a ZIP entry";
			return false;
		}
		entries.push_back({position, info, *normalized, is_directory, is_symlink, is_special});
		if (!is_directory)
		{
			files.emplace_back(*normalized);
		}

		if (index + 1 < global.number_entry && unzGoToNextFile(archive) != UNZ_OK)
		{
			detail = "The ZIP directory ended unexpectedly";
			return false;
		}
	}
	return true;
}

bool ignored_zip_metadata(std::string_view relative_path)
{
	if (relative_path == "__MACOSX" || relative_path.starts_with("__MACOSX/"))
	{
		return true;
	}
	const usz separator = relative_path.rfind('/');
	const std::string_view name = separator == std::string_view::npos
		? relative_path
		: relative_path.substr(separator + 1);
	return name == ".DS_Store" || name.starts_with("._");
}

bool selected_zip_entry(
	const zip_entry& entry,
	const game_archive_layout& layout,
	std::string_view& relative_path)
{
	if (!layout.prefix.empty() && !entry.path.starts_with(layout.prefix))
	{
		return false;
	}
	relative_path = std::string_view{entry.path}.substr(layout.prefix.size());
	return !relative_path.empty() && !ignored_zip_metadata(relative_path);
}

bool extract_zip_entry(
	unzFile archive,
	const zip_entry& entry,
	const std::string& destination,
	u64& extracted,
	u64 total,
	const game_zip_progress_callback& progress,
	std::string& detail)
{
	if ((entry.info.flag & 1) != 0)
	{
		detail = fmt::format("Password-protected ZIP entries are not supported (%s)", entry.path);
		return false;
	}
	if (entry.info.compression_method != 0 && entry.info.compression_method != Z_DEFLATED)
	{
		detail = fmt::format("The ZIP uses unsupported compression for %s", entry.path);
		return false;
	}
	if (unzGoToFilePos64(archive, &entry.position) != UNZ_OK ||
		unzOpenCurrentFile(archive) != UNZ_OK)
	{
		detail = fmt::format("RPCS3 could not open %s inside the ZIP", entry.path);
		return false;
	}

	fs::file output{destination, fs::rewrite};
	if (!output)
	{
		unzCloseCurrentFile(archive);
		detail = fmt::format("RPCS3 could not create %s", entry.path);
		return false;
	}

	std::vector<u8> buffer(1024 * 1024);
	u64 written = 0;
	for (;;)
	{
		const int count = unzReadCurrentFile(archive, buffer.data(), buffer.size());
		if (count < 0)
		{
			unzCloseCurrentFile(archive);
			detail = fmt::format("The compressed data for %s is corrupt", entry.path);
			return false;
		}
		if (!count)
		{
			break;
		}
		if (output.write(buffer.data(), static_cast<usz>(count)) != static_cast<usz>(count))
		{
			unzCloseCurrentFile(archive);
			detail = fmt::format("RPCS3 could not write %s", entry.path);
			return false;
		}
		written += static_cast<u64>(count);
		extracted += static_cast<u64>(count);
		if (written > entry.info.uncompressed_size || extracted > total)
		{
			unzCloseCurrentFile(archive);
			detail = fmt::format("The ZIP reported an invalid size for %s", entry.path);
			return false;
		}
		if (progress)
		{
			const u32 scaled = total
				? static_cast<u32>(std::min<long double>(900,
					static_cast<long double>(extracted) * 900 / total))
				: 900;
			progress(scaled, 1000, fmt::format("Extracting %s (%s of %s)", entry.path,
				formatted_byte_size(extracted), formatted_byte_size(total)));
		}
	}

	const int close_result = unzCloseCurrentFile(archive);
	if (close_result != UNZ_OK || written != entry.info.uncompressed_size)
	{
		detail = close_result == UNZ_CRCERROR
			? fmt::format("The ZIP checksum failed for %s", entry.path)
			: fmt::format("The ZIP ended early while extracting %s", entry.path);
		return false;
	}
	return true;
}

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
				? static_cast<u32>(std::min<long double>(900,
					static_cast<long double>(copied) * 900 / total))
				: 900;
			progress(scaled, 1000, fmt::format("%s (%s of %s)", stage,
				formatted_byte_size(copied), formatted_byte_size(total)));
		}
	}

	return source.pos() == source.size();
}

bool copy_folder_file_with_progress(
	const std::string& source_path,
	const std::string& destination_path,
	std::string_view relative_path,
	u64 expected_size,
	u64& copied,
	u64 total,
	const game_folder_progress_callback& progress)
{
	fs::file source{source_path, fs::read};
	fs::file destination{destination_path, fs::rewrite};
	if (!source || !destination || source.size() != expected_size)
	{
		return false;
	}

	std::vector<u8> buffer(4 * 1024 * 1024);
	u64 file_copied = 0;
	while (file_copied < expected_size)
	{
		const u64 remaining = expected_size - file_copied;
		const u64 count = source.read(buffer.data(), std::min<u64>(buffer.size(), remaining));
		if (!count || count > remaining || destination.write(buffer.data(), count) != count)
		{
			return false;
		}

		file_copied += count;
		copied += count;
		if (progress)
		{
			const u32 scaled = total
				? static_cast<u32>(std::min<long double>(900,
					static_cast<long double>(copied) * 900 / total))
				: 900;
			progress(scaled, 1000, fmt::format("Copying %s (%s of %s)", relative_path,
				formatted_byte_size(copied), formatted_byte_size(total)));
		}
	}

	u8 extra = 0;
	return source.read(&extra, 1) == 0 && source.pos() == source.size();
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
		psf::get_integer(metadata, "RESOLUTION", 0),
		iso_path,
		std::move(icon_path),
		std::string{psf::get_string(metadata, "PS3_SYSTEM_VER")},
		psf::get_integer(metadata, "ATTRIBUTE", 0),
		psf::get_integer(metadata, "BOOTABLE", 0),
		psf::get_integer(metadata, "PARENTAL_LEVEL", 0),
		psf::get_integer(metadata, "SOUND_FORMAT", 0),
	};
}

std::optional<installed_game> installed_extracted_game(const std::string& directory)
{
	const bool is_disc = fs::is_file(directory + "/PS3_GAME/PARAM.SFO") &&
		fs::is_file(directory + "/PS3_GAME/USRDIR/EBOOT.BIN");
	const std::string content_root = is_disc ? directory + "/PS3_GAME" : directory;
	const std::string sfo_path = content_root + "/PARAM.SFO";
	if (!fs::is_file(sfo_path) || !fs::is_file(content_root + "/USRDIR/EBOOT.BIN"))
	{
		return std::nullopt;
	}

	const psf::registry metadata = psf::load_object(sfo_path);
	std::string title_id{psf::get_string(metadata, "TITLE_ID")};
	std::string category{psf::get_string(metadata, "CATEGORY")};
	if (!valid_title_id(title_id) ||
		(is_disc ? category != "DG" : !psf::is_cat_hdd(category)))
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
	std::string icon_path = content_root + "/ICON0.PNG";
	if (!fs::is_file(icon_path))
	{
		icon_path.clear();
	}

	return installed_game{
		std::move(title_id),
		std::move(title),
		std::move(version),
		std::move(category),
		psf::get_integer(metadata, "RESOLUTION", 0),
		directory,
		std::move(icon_path),
		std::string{psf::get_string(metadata, "PS3_SYSTEM_VER")},
		psf::get_integer(metadata, "ATTRIBUTE", 0),
		psf::get_integer(metadata, "BOOTABLE", 0),
		psf::get_integer(metadata, "PARENTAL_LEVEL", 0),
		psf::get_integer(metadata, "SOUND_FORMAT", 0),
	};
}
}

bool is_valid_game_title_id(std::string_view title_id) noexcept
{
	return valid_title_id(title_id);
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

game_patch_install_result install_game_patch(
	std::string_view expected_title_id,
	const std::string& package_path,
	const game_package_progress_callback& progress)
{
	if (!valid_title_id(expected_title_id))
	{
		return invalid_patch("The selected game has an invalid title ID");
	}
	if (!find_installed_game(expected_title_id))
	{
		return patch_installation_failed(
			fmt::format("Game %s is not installed", expected_title_id),
			std::string{expected_title_id});
	}
	if (package_path.empty() || package_path.front() != '/')
	{
		return invalid_patch("The update-package path must be an absolute sandbox path");
	}

	package_reader reader{package_path};
	if (!reader.is_valid() || reader.get_header().pkg_platform != PKG_PLATFORM_TYPE_PS3)
	{
		return invalid_patch("The selected file is not a valid PlayStation 3 update package");
	}

	const psf::registry& package_psf = reader.get_psf();
	std::string package_title_id{psf::get_string(package_psf, "TITLE_ID")};
	std::string title{psf::get_string(package_psf, "TITLE")};
	std::string version{psf::get_string(package_psf, "APP_VER")};
	const std::string category{psf::get_string(package_psf, "CATEGORY")};
	const bool has_patch_flag =
		(static_cast<u32>(reader.get_metadata().package_type) & pkg_flag::PKG_FLAG_PATCH) != 0;
	if (title.empty())
	{
		title = package_title_id.empty() ? "PlayStation 3 game update" : package_title_id;
	}

	switch (validate_game_patch_contract(
		expected_title_id, package_title_id, category, has_patch_flag))
	{
	case game_patch_validation_error::invalid_patch:
		return invalid_patch(
			"The selected package is not a PlayStation 3 game update",
			std::move(package_title_id), std::move(title), std::move(version));
	case game_patch_validation_error::title_mismatch:
		return patch_title_mismatch(fmt::format(
			"The selected update is for %s, not %s", package_title_id, expected_title_id),
			std::move(package_title_id), std::move(title), std::move(version));
	case game_patch_validation_error::none:
		break;
	}

	const game_package_install_result result = install_game_package(package_path, progress);
	if (result.error == game_package_install_error::invalid_package)
	{
		return invalid_patch(result.detail, std::move(package_title_id),
			std::move(title), std::move(version));
	}
	if (result.error != game_package_install_error::none)
	{
		return patch_installation_failed(result.detail, std::move(package_title_id),
			std::move(title), std::move(version));
	}
	return {
		game_patch_install_error::none,
		std::move(package_title_id),
		std::move(title),
		std::move(version),
		{},
	};
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

game_zip_install_result install_game_zip(
	const std::string& zip_path,
	const game_zip_progress_callback& progress)
{
	if (zip_path.empty() || zip_path.front() != '/')
	{
		return invalid_zip("The ZIP path must be an absolute sandbox path");
	}

	zip_handle archive{unzOpen64(zip_path.c_str())};
	if (!archive.value)
	{
		return invalid_zip("The selected file is not a readable ZIP archive");
	}
	if (progress)
	{
		progress(0, 1000, "Inspecting ZIP archive");
	}

	std::vector<zip_entry> entries;
	std::vector<std::string> files;
	std::string failure_detail;
	if (!enumerate_zip_entries(archive.value, entries, files, failure_detail))
	{
		return invalid_zip(std::move(failure_detail));
	}
	const auto layout = detect_game_archive_layout(files);
	if (!layout)
	{
		return invalid_zip("The ZIP must contain exactly one bootable PlayStation 3 game folder with PARAM.SFO and USRDIR/EBOOT.BIN");
	}

	u64 required_size = 0;
	for (const zip_entry& entry : entries)
	{
		std::string_view relative_path;
		if (!selected_zip_entry(entry, *layout, relative_path))
		{
			continue;
		}
		if (entry.is_symlink || entry.is_special)
		{
			return invalid_zip(fmt::format("The ZIP contains an unsupported link or special file: %s", entry.path));
		}
		if ((entry.info.flag & 1) != 0)
		{
			return invalid_zip(fmt::format("Password-protected ZIP entries are not supported (%s)", entry.path));
		}
		if (!entry.is_directory && entry.info.compression_method != 0 &&
			entry.info.compression_method != Z_DEFLATED)
		{
			return invalid_zip(fmt::format("The ZIP uses unsupported compression for %s", entry.path));
		}
		if (!entry.is_directory)
		{
			if (entry.info.uncompressed_size > std::numeric_limits<u64>::max() - required_size)
			{
				return invalid_zip("The ZIP reports an invalid extracted size");
			}
			required_size += entry.info.uncompressed_size;
		}
	}
	if (!required_size)
	{
		return invalid_zip("The ZIP contains no extractable game data");
	}

	const std::string root = extracted_game_root();
	if (!fs::create_path(root))
	{
		return zip_installation_failed("Unable to create the extracted-game library");
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
		return zip_installation_failed("Unable to determine free space for ZIP installation");
	}
	if (device.avail_free < required_size)
	{
		return zip_installation_failed(fmt::format(
			"Not enough free space to extract the ZIP (need at least %s more)",
			formatted_byte_size(required_size - device.avail_free)));
	}

	static std::atomic<u64> import_counter{0};
	temporary_directory temporary{
		root + fmt::format(".import-%x", import_counter.fetch_add(1, std::memory_order_relaxed))
	};
	if (!fs::create_dir(temporary.path))
	{
		return zip_installation_failed("Unable to create temporary ZIP installation storage");
	}

	u64 extracted = 0;
	for (const zip_entry& entry : entries)
	{
		std::string_view relative_path;
		if (!selected_zip_entry(entry, *layout, relative_path))
		{
			continue;
		}
		const std::string destination = temporary.path + "/" + std::string{relative_path};
		if (entry.is_directory)
		{
			if (!fs::create_path(destination + "/"))
			{
				return zip_installation_failed(fmt::format(
					"Unable to create the directory %s", relative_path));
			}
			continue;
		}

		const usz separator = relative_path.rfind('/');
		if (separator != std::string_view::npos &&
			!fs::create_path(temporary.path + "/" + std::string{relative_path.substr(0, separator + 1)}))
		{
			return zip_installation_failed(fmt::format(
				"Unable to create storage for %s", relative_path));
		}
		if (!extract_zip_entry(archive.value, entry, destination, extracted,
			required_size, progress, failure_detail))
		{
			return invalid_zip(std::move(failure_detail));
		}
	}

	if (progress)
	{
		progress(920, 1000, "Validating extracted PlayStation 3 game");
	}
	const bool is_disc = layout->kind == game_archive_layout_kind::disc_folder;
	const std::string content_root = is_disc ? temporary.path + "/PS3_GAME" : temporary.path;
	const std::string sfo_path = content_root + "/PARAM.SFO";
	const psf::registry metadata = psf::load_object(sfo_path);
	std::string title_id{psf::get_string(metadata, "TITLE_ID")};
	std::string title{psf::get_string(metadata, "TITLE")};
	const std::string category{psf::get_string(metadata, "CATEGORY")};
	if (!valid_title_id(title_id) || !fs::is_file(content_root + "/USRDIR/EBOOT.BIN") ||
		(is_disc ? category != "DG" : !psf::is_cat_hdd(category)))
	{
		return invalid_zip("The extracted folder is not a bootable PlayStation 3 game");
	}
	if (title.empty())
	{
		title = title_id;
	}

	if (find_installed_game(title_id))
	{
		return zip_installation_failed(
			fmt::format("A game with title ID %s is already installed", title_id), title_id, title);
	}
	const std::string final_directory = root + title_id;
	if (fs::is_dir(final_directory) || !fs::rename(temporary.path, final_directory, false))
	{
		return zip_installation_failed("Unable to finalize the ZIP installation", title_id, title);
	}
	temporary.path.clear();

	if (progress)
	{
		progress(1000, 1000, fmt::format("Installed %s", title));
	}
	return {game_zip_install_error::none, std::move(title_id), std::move(title), {}};
}

game_folder_install_result install_game_folder(
	const std::string& folder_path,
	const game_folder_progress_callback& progress)
{
	if (folder_path.empty() || folder_path.front() != '/')
	{
		return invalid_folder("The game-folder path must be an absolute security-scoped path");
	}

	std::string source_root = folder_path;
	while (source_root.size() > 1 && source_root.ends_with('/'))
	{
		source_root.pop_back();
	}
	struct stat root_info{};
	if (::lstat(source_root.c_str(), &root_info) != 0 ||
		S_ISLNK(root_info.st_mode) || !S_ISDIR(root_info.st_mode))
	{
		return invalid_folder("The selected item is not a readable folder");
	}
	if (progress)
	{
		progress(0, 1000, "Inspecting selected game folder");
	}

	std::vector<folder_tree_entry> entries;
	std::vector<std::string> files;
	u64 required_size = 0;
	std::string failure_detail;
	if (!enumerate_folder_directory(
		source_root, {}, 0, entries, files, required_size, failure_detail))
	{
		return invalid_folder(std::move(failure_detail));
	}
	const auto layout = detect_game_folder_layout(files);
	if (!layout)
	{
		return invalid_folder(
			"Select a game folder containing either PS3_GAME/PARAM.SFO and PS3_GAME/USRDIR/EBOOT.BIN, or PARAM.SFO and USRDIR/EBOOT.BIN");
	}

	const psf::registry metadata = psf::load_object(source_root + "/" + layout->metadata_path);
	std::string title_id{psf::get_string(metadata, "TITLE_ID")};
	std::string title{psf::get_string(metadata, "TITLE")};
	const std::string category{psf::get_string(metadata, "CATEGORY")};
	const bool is_disc_category = category == "DG";
	const bool is_hdd_category = psf::is_cat_hdd(category);
	const auto install_prefix = game_folder_install_prefix(
		layout->kind, is_disc_category, is_hdd_category);
	if (!valid_title_id(title_id) || !install_prefix)
	{
		return invalid_folder("The selected folder is not a bootable PlayStation 3 game");
	}
	if (title.empty())
	{
		title = title_id;
	}
	if (find_installed_game(title_id))
	{
		return folder_installation_failed(
			fmt::format("A game with title ID %s is already installed", title_id), title_id, title);
	}

	const std::string root = extracted_game_root();
	if (!fs::create_path(root))
	{
		return folder_installation_failed("Unable to create the extracted-game library", title_id, title);
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
		return folder_installation_failed(
			"Unable to determine free space for folder installation", title_id, title);
	}
	if (device.avail_free < required_size)
	{
		return folder_installation_failed(fmt::format(
			"Not enough free space to copy the game folder (need at least %s more)",
			formatted_byte_size(required_size - device.avail_free)), title_id, title);
	}

	static std::atomic<u64> import_counter{0};
	temporary_directory temporary{
		root + fmt::format(".import-%x", import_counter.fetch_add(1, std::memory_order_relaxed))
	};
	if (!fs::create_dir(temporary.path))
	{
		return folder_installation_failed(
			"Unable to create temporary folder installation storage", title_id, title);
	}

	const std::string destination_root = temporary.path + "/" + std::string{*install_prefix};
	if (!fs::create_path(destination_root))
	{
		return folder_installation_failed(
			"Unable to prepare temporary game-folder storage", title_id, title);
	}
	u64 copied = 0;
	for (const folder_tree_entry& entry : entries)
	{
		const std::string destination = destination_root + entry.relative_path;
		if (entry.is_directory)
		{
			if (!fs::create_path(destination + "/"))
			{
				return folder_installation_failed(
					fmt::format("Unable to create storage for %s", entry.relative_path),
					title_id, title);
			}
			continue;
		}

		if (!copy_folder_file_with_progress(
			source_root + "/" + entry.relative_path,
			destination,
			entry.relative_path,
			entry.size,
			copied,
			required_size,
			progress))
		{
			return folder_installation_failed(
				fmt::format("Unable to copy %s from the selected folder", entry.relative_path),
				title_id, title);
		}
	}

	if (progress)
	{
		progress(920, 1000, "Validating copied PlayStation 3 game");
	}
	const auto copied_game = installed_extracted_game(temporary.path);
	if (!copied_game || copied_game->title_id != title_id)
	{
		return invalid_folder("The copied folder is not a bootable PlayStation 3 game");
	}

	const std::string final_directory = root + title_id;
	if (fs::is_dir(final_directory) || !fs::rename(temporary.path, final_directory, false))
	{
		return folder_installation_failed(
			"Unable to finalize the game-folder installation", title_id, title);
	}
	temporary.path.clear();

	if (progress)
	{
		progress(1000, 1000, fmt::format("Installed %s", title));
	}
	return {game_folder_install_error::none, std::move(title_id), std::move(title), {}};
}

game_delete_result delete_installed_game(std::string_view title_id)
{
	if (!valid_title_id(title_id))
	{
		return {
			game_delete_error::not_found,
			std::string{title_id},
			{},
			"The selected game is not installed",
		};
	}

	const auto installed = find_installed_game(title_id);
	if (!installed)
	{
		return {
			game_delete_error::not_found,
			std::string{title_id},
			{},
			"The selected game is not installed",
		};
	}

	const std::string id{title_id};
	std::string detail;
	if (!remove_installed_paths_with_prefix(
		rpcs3::utils::get_hdd0_game_dir(), id, detail))
	{
		return {
			game_delete_error::deletion_failed,
			id,
			installed->title,
			std::move(detail),
		};
	}
	const std::array private_install_paths{
		disc_image_root() + id,
		extracted_game_root() + id,
	};
	for (const std::string& path : private_install_paths)
	{
		if (!remove_installed_path(path, detail))
		{
			return {
				game_delete_error::deletion_failed,
				id,
				installed->title,
				std::move(detail),
			};
		}
	}

	// Match RPCS3's desktop removal defaults: discard derived title state and
	// custom configuration, but retain save data and savestates for recovery or
	// a later reinstall.
	const std::array derived_paths{
		rpcs3::utils::get_cache_dir_by_serial(id),
		rpcs3::utils::get_custom_config_path(id),
		game_settings_preset_directory(id),
		rpcs3::utils::get_input_config_dir(id),
	};
	for (const std::string& path : derived_paths)
	{
		if (!remove_installed_path(path, detail))
		{
			return {
				game_delete_error::deletion_failed,
				id,
				installed->title,
				std::move(detail),
			};
		}
	}
	if (!remove_installed_paths_with_prefix(
		rpcs3::utils::get_hdd1_cache_dir(), id, detail) ||
		!remove_installed_paths_with_prefix(
		rpcs3::utils::get_hdd0_locks_dir(), id, detail))
	{
		return {
			game_delete_error::deletion_failed,
			id,
			installed->title,
			std::move(detail),
		};
	}

	return {
		game_delete_error::none,
		id,
		installed->title,
		{},
	};
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
		if (!valid_title_id(title_id) || !psf::is_cat_hdd(category))
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
			psf::get_integer(metadata, "RESOLUTION", 0),
			path,
			std::move(icon_path),
			std::string{psf::get_string(metadata, "PS3_SYSTEM_VER")},
			psf::get_integer(metadata, "ATTRIBUTE", 0),
			psf::get_integer(metadata, "BOOTABLE", 0),
			psf::get_integer(metadata, "PARENTAL_LEVEL", 0),
			psf::get_integer(metadata, "SOUND_FORMAT", 0),
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

	const std::string archive_root = extracted_game_root();
	for (auto&& entry : fs::dir{archive_root})
	{
		if (!entry.is_directory || entry.name == "." || entry.name == ".." ||
			entry.name.starts_with(".import-"))
		{
			continue;
		}
		if (auto game = installed_extracted_game(archive_root + entry.name))
		{
			result.emplace_back(std::move(*game));
		}
	}

	// Match the Qt game list: keep the disc/private base as the boot entry, but
	// display the newer cumulative update version installed in dev_hdd0.
	for (installed_game& game : result)
	{
		if (game.category == "GD")
		{
			continue;
		}

		const std::string update_sfo_path = game_root + game.title_id + "/PARAM.SFO";
		if (!fs::is_file(update_sfo_path))
		{
			continue;
		}

		const psf::registry update_metadata = psf::load_object(update_sfo_path);
		if (psf::get_string(update_metadata, "TITLE_ID") != game.title_id ||
			psf::get_string(update_metadata, "CATEGORY") != "GD")
		{
			continue;
		}

		std::string update_version{psf::get_string(update_metadata, "APP_VER")};
		if (update_version.empty())
		{
			update_version = std::string{psf::get_string(update_metadata, "VERSION")};
		}
		if (!update_version.empty() && (game.version.empty() ||
			rpcs3::utils::version_is_bigger(update_version, game.version, game.title_id, false)))
		{
			game.version = std::move(update_version);

			const std::string update_firmware{psf::get_string(update_metadata, "PS3_SYSTEM_VER")};
			if (!update_firmware.empty() && (game.firmware_version.empty() ||
				rpcs3::utils::version_is_bigger(update_firmware, game.firmware_version, game.title_id, true)))
			{
				game.firmware_version = update_firmware;
			}

			const u32 update_parental_level = psf::get_integer(update_metadata, "PARENTAL_LEVEL", 0);
			if (update_parental_level > game.parental_level)
			{
				game.parental_level = update_parental_level;
			}
		}
	}

	// Match the Qt game-list size column. Disc images report their file size;
	// directory-backed games report the recursive size of their boot path.
	for (installed_game& game : result)
	{
		if (fs::is_file(game.path))
		{
			const fs::file file{game.path};
			game.size_on_disk = file ? file.size() : umax;
		}
		else
		{
			game.size_on_disk = fs::get_dir_size(game.path, 1);
		}
	}

	std::sort(result.begin(), result.end(), [](const installed_game& left, const installed_game& right)
	{
		if (left.title_id != right.title_id)
		{
			return left.title_id < right.title_id;
		}
		const bool left_is_patch = left.category == "GD";
		const bool right_is_patch = right.category == "GD";
		if (left_is_patch != right_is_patch)
		{
			return !left_is_patch;
		}
		return left.path < right.path;
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

std::vector<installed_game_patch> installed_game_patches(std::string_view title_id)
{
	if (!valid_title_id(title_id))
	{
		return {};
	}

	const std::string path = rpcs3::utils::get_hdd0_dir() +
		"game/" + std::string{title_id};
	const std::string sfo_path = path + "/PARAM.SFO";
	if (!fs::is_file(sfo_path))
	{
		return {};
	}

	const psf::registry metadata = psf::load_object(sfo_path);
	const std::string installed_title_id{psf::get_string(metadata, "TITLE_ID")};
	if (installed_title_id != title_id || psf::get_string(metadata, "CATEGORY") != "GD")
	{
		return {};
	}

	std::string title{psf::get_string(metadata, "TITLE")};
	if (title.empty())
	{
		title = installed_title_id;
	}
	std::string version{psf::get_string(metadata, "APP_VER")};
	if (version.empty())
	{
		version = std::string{psf::get_string(metadata, "VERSION")};
	}
	return {{std::move(installed_title_id), std::move(title), std::move(version)}};
}
}
