#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rpcs3::ios
{
enum class game_archive_layout_kind
{
	disc_folder,
	hdd_folder,
};

struct game_archive_layout
{
	game_archive_layout_kind kind = game_archive_layout_kind::disc_folder;
	std::string prefix;
	std::string metadata_path;
	std::string executable_path;
};

inline std::optional<std::string> normalize_game_archive_path(
	std::string_view raw_path,
	bool is_directory)
{
	if (raw_path.empty() || raw_path.size() > 4096 || raw_path.front() == '/' ||
		raw_path.find('\0') != std::string_view::npos ||
		raw_path.find('\\') != std::string_view::npos)
	{
		return std::nullopt;
	}

	while (is_directory && raw_path.ends_with('/'))
	{
		raw_path.remove_suffix(1);
	}
	if (raw_path.empty() || (!is_directory && raw_path.ends_with('/')))
	{
		return std::nullopt;
	}

	for (std::size_t start = 0; start <= raw_path.size();)
	{
		const std::size_t separator = raw_path.find('/', start);
		const std::size_t end = separator == std::string_view::npos ? raw_path.size() : separator;
		const std::string_view component = raw_path.substr(start, end - start);
		if (component.empty() || component == "." || component == ".." ||
			component.size() > 255 || component.find(':') != std::string_view::npos)
		{
			return std::nullopt;
		}
		if (separator == std::string_view::npos)
		{
			break;
		}
		start = separator + 1;
	}

	return std::string{raw_path};
}

inline std::optional<game_archive_layout> detect_game_archive_layout(
	std::span<const std::string> file_paths)
{
	const std::unordered_set<std::string> files{file_paths.begin(), file_paths.end()};
	std::vector<game_archive_layout> candidates;

	auto add_candidate = [&candidates](game_archive_layout candidate)
	{
		const auto duplicate = std::find_if(candidates.begin(), candidates.end(),
			[&candidate](const game_archive_layout& existing)
			{
				return existing.kind == candidate.kind && existing.prefix == candidate.prefix;
			});
		if (duplicate == candidates.end())
		{
			candidates.emplace_back(std::move(candidate));
		}
	};

	constexpr std::string_view disc_metadata = "PS3_GAME/PARAM.SFO";
	for (const std::string& path : file_paths)
	{
		if (path != disc_metadata && !path.ends_with(std::string{"/"} + std::string{disc_metadata}))
		{
			continue;
		}
		const std::string prefix = path.substr(0, path.size() - disc_metadata.size());
		const std::string executable = prefix + "PS3_GAME/USRDIR/EBOOT.BIN";
		if (files.contains(executable))
		{
			add_candidate({game_archive_layout_kind::disc_folder, prefix, path, executable});
		}
	}

	constexpr std::string_view hdd_metadata = "PARAM.SFO";
	for (const std::string& path : file_paths)
	{
		if (path != hdd_metadata && !path.ends_with(std::string{"/"} + std::string{hdd_metadata}))
		{
			continue;
		}
		const std::string prefix = path.substr(0, path.size() - hdd_metadata.size());
		if (prefix.ends_with("PS3_GAME/"))
		{
			continue;
		}
		const std::string executable = prefix + "USRDIR/EBOOT.BIN";
		if (files.contains(executable))
		{
			add_candidate({game_archive_layout_kind::hdd_folder, prefix, path, executable});
		}
	}

	if (candidates.size() != 1)
	{
		return std::nullopt;
	}
	return candidates.front();
}
}
