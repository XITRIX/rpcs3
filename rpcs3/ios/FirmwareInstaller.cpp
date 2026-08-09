#include "stdafx.h"

#include "FirmwareInstaller.h"

#include "Crypto/key_vault.h"
#include "Crypto/unself.h"
#include "Emu/System.h"
#include "Emu/VFS.h"
#include "Emu/vfs_config.h"
#include "Loader/PUP.h"
#include "Loader/TAR.h"
#include "Utilities/File.h"
#include "Utilities/StrFmt.h"
#include "util/sysinfo.hpp"

#include <algorithm>

namespace rpcs3::ios
{
namespace
{
firmware_install_result invalid_firmware(std::string detail)
{
	return {firmware_install_error::invalid_firmware, {}, std::move(detail)};
}

firmware_install_result installation_failed(std::string detail)
{
	return {firmware_install_error::installation_failed, {}, std::move(detail)};
}

firmware_install_result pup_validation_error(const pup_object& pup)
{
	switch (static_cast<pup_error>(pup))
	{
	case pup_error::header_read:
		return invalid_firmware("The selected firmware file is empty or too small");
	case pup_error::header_magic:
		return invalid_firmware("The selected file is not a PlayStation 3 PUP file");
	case pup_error::expected_size:
		return invalid_firmware("The selected firmware file is incomplete");
	case pup_error::hash_mismatch:
		return invalid_firmware("The selected firmware file failed its integrity check");
	case pup_error::header_file_count:
	case pup_error::file_entries:
	case pup_error::stream:
		return invalid_firmware(pup.get_formatted_error().empty()
			? "The selected firmware file is corrupted"
			: pup.get_formatted_error());
	case pup_error::ok:
		break;
	}

	return {};
}
}

std::string firmware_version()
{
	return utils::get_firmware_version();
}

firmware_install_result install_firmware(const std::string& path, const firmware_progress_callback& progress)
{
	if (path.empty() || path.front() != '/')
	{
		return invalid_firmware("The firmware path must be an absolute sandbox path");
	}

	fs::file pup_file(path);
	if (!pup_file)
	{
		return invalid_firmware(fmt::format("Unable to open the selected firmware file: %s", fs::g_tls_error));
	}

	pup_object pup(std::move(pup_file));
	if (const auto validation = pup_validation_error(pup); validation.error != firmware_install_error::none)
	{
		return validation;
	}

	fs::file update_files_file = pup.get_file(0x300);
	const usz update_files_size = update_files_file ? update_files_file.size() : 0;
	if (!update_files_size)
	{
		return invalid_firmware("The firmware installation package database is missing");
	}

	fs::device_stat device{};
	const std::string dev_flash = g_cfg_vfs.get_dev_flash();
	if (!fs::statfs(dev_flash, device))
	{
		return installation_failed("Unable to determine free space for the firmware installation");
	}
	if (device.avail_free < update_files_size)
	{
		return installation_failed(fmt::format(
			"Not enough free space to install the firmware (need at least %u more bytes)",
			update_files_size - device.avail_free));
	}

	tar_object update_files(update_files_file);
	auto update_filenames = update_files.get_filenames();
	update_filenames.erase(std::remove_if(update_filenames.begin(), update_filenames.end(), [](const std::string& name)
	{
		return name.find("dev_flash_") == umax;
	}), update_filenames.end());

	if (update_filenames.empty())
	{
		return invalid_firmware("The firmware contains no dev_flash installation packages");
	}

	std::string package_version;
	if (fs::file version_file = pup.get_file(0x100))
	{
		package_version = version_file.to_string();
	}
	if (const usz newline = package_version.find_first_of("\r\n"); newline != umax)
	{
		package_version.erase(newline);
	}
	if (package_version.empty())
	{
		return invalid_firmware("The firmware version record is missing");
	}

	if (!vfs::mount("/dev_flash", dev_flash))
	{
		return installation_failed("Unable to mount the firmware installation directory");
	}

	const u32 total = ::narrow<u32>(update_filenames.size());
	if (progress)
	{
		progress(0, total, fmt::format("Installing PS3 firmware %s", package_version));
	}

	firmware_install_result result{};
	for (u32 index = 0; index < total; index++)
	{
		const std::string& update_filename = update_filenames[index];
		auto update_file_stream = update_files.get_file(update_filename);
		if (!update_file_stream)
		{
			result = invalid_firmware(fmt::format("Unable to read firmware package %s", update_filename));
			break;
		}

		if (update_file_stream->m_file_handler)
		{
			update_file_stream->m_file_handler->handle_file_op(
				*update_file_stream, 0, update_file_stream->get_size(umax), nullptr);
		}

		fs::file update_file = fs::make_stream(std::move(update_file_stream->data));
		SCEDecrypter decrypter(update_file);
		decrypter.LoadHeaders();
		decrypter.LoadMetadata(SCEPKG_ERK, SCEPKG_RIV);
		decrypter.DecryptData();

		auto dev_flash_files = decrypter.MakeFile();
		if (dev_flash_files.size() < 3)
		{
			result = invalid_firmware(fmt::format("Unable to decrypt firmware package %s", update_filename));
			break;
		}

		tar_object dev_flash_tar(dev_flash_files[2]);
		if (!dev_flash_tar.extract())
		{
			result = invalid_firmware(fmt::format("Unable to extract firmware package %s", update_filename));
			break;
		}

		if (progress)
		{
			progress(index + 1, total, fmt::format("Installed firmware package %u of %u", index + 1, total));
		}
	}

	update_files_file.close();
	// Refresh RPCS3's VFS state after the temporary installation mount, matching
	// the desktop installer's post-install behavior.
	Emu.Init();

	if (result.error != firmware_install_error::none)
	{
		return result;
	}

	result.version = firmware_version();
	if (result.version.empty())
	{
		return installation_failed("Firmware extraction completed, but its installed version could not be verified");
	}

	return result;
}
}
