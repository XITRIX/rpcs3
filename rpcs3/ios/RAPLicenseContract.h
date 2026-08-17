#pragma once

#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace rpcs3::ios
{
inline std::optional<std::string> normalized_rap_license_filename(std::string_view path)
{
	if (path.empty() || path.front() != '/')
	{
		return std::nullopt;
	}

	const std::string_view filename = path.substr(path.find_last_of('/') + 1);
	if (filename.size() <= 4 || filename[filename.size() - 4] != '.')
	{
		return std::nullopt;
	}

	const std::string_view extension = filename.substr(filename.size() - 3);
	if (std::tolower(static_cast<unsigned char>(extension[0])) != 'r' ||
		std::tolower(static_cast<unsigned char>(extension[1])) != 'a' ||
		std::tolower(static_cast<unsigned char>(extension[2])) != 'p')
	{
		return std::nullopt;
	}

	return std::string{filename.substr(0, filename.size() - 4)} + ".rap";
}
}
