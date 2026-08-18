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

inline bool is_lexically_within_path(std::string_view root, std::string_view candidate)
{
	const std::string normalized_root = normalize_resolved_host_path(root);
	if (normalized_root.empty() || normalized_root.front() != '/' ||
		candidate.empty() || candidate.front() != '/')
	{
		return false;
	}

	const std::string prefix = normalized_root == "/" ? normalized_root : normalized_root + '/';
	if (!candidate.starts_with(prefix) || candidate.size() == prefix.size())
	{
		return false;
	}

	std::string_view relative = candidate.substr(prefix.size());
	while (!relative.empty())
	{
		const std::size_t separator = relative.find('/');
		const std::string_view component = relative.substr(0, separator);
		if (component.empty() || component == "." || component == "..")
		{
			return false;
		}
		if (separator == std::string_view::npos)
		{
			break;
		}
		relative.remove_prefix(separator + 1);
	}
	return true;
}
}
