#pragma once

#include <string>
#include <string_view>

namespace rpcs3::ios
{
inline std::string normalize_resolved_host_path(std::string_view path)
{
	std::string result{path};

	// Match QFileInfo::canonicalFilePath(): directory paths do not retain a
	// trailing separator, except for the filesystem root.
	while (result.size() > 1 && result.back() == '/')
	{
		result.pop_back();
	}

	return result;
}
}
