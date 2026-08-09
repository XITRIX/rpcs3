#pragma once

#include "util/types.hpp"

#include <functional>
#include <string>
#include <string_view>

namespace rpcs3::ios
{
enum class firmware_install_error
{
	none,
	invalid_firmware,
	installation_failed,
};

struct firmware_install_result
{
	firmware_install_error error = firmware_install_error::none;
	std::string version;
	std::string detail;
};

using firmware_progress_callback = std::function<void(u32 completed, u32 total, std::string_view stage)>;

std::string firmware_version();
firmware_install_result install_firmware(const std::string& path, const firmware_progress_callback& progress);
}
