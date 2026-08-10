#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>

namespace rpcs3::ios
{
enum class game_folder_layout_kind
{
	disc_root,
	content_root,
};

struct game_folder_layout
{
	game_folder_layout_kind kind = game_folder_layout_kind::disc_root;
	std::string metadata_path;
	std::string executable_path;
};

inline std::optional<game_folder_layout> detect_game_folder_layout(
	std::span<const std::string> file_paths)
{
	const std::unordered_set<std::string> files{file_paths.begin(), file_paths.end()};
	const bool has_disc_root = files.contains("PS3_GAME/PARAM.SFO") &&
		files.contains("PS3_GAME/USRDIR/EBOOT.BIN");
	const bool has_content_root = files.contains("PARAM.SFO") &&
		files.contains("USRDIR/EBOOT.BIN");

	if (has_disc_root == has_content_root)
	{
		return std::nullopt;
	}
	if (has_disc_root)
	{
		return game_folder_layout{
			game_folder_layout_kind::disc_root,
			"PS3_GAME/PARAM.SFO",
			"PS3_GAME/USRDIR/EBOOT.BIN",
		};
	}
	return game_folder_layout{
		game_folder_layout_kind::content_root,
		"PARAM.SFO",
		"USRDIR/EBOOT.BIN",
	};
}

inline std::optional<std::string_view> game_folder_install_prefix(
	game_folder_layout_kind layout,
	bool is_disc_category,
	bool is_hdd_category)
{
	if (is_disc_category == is_hdd_category)
	{
		return std::nullopt;
	}
	if (layout == game_folder_layout_kind::disc_root)
	{
		return is_disc_category
			? std::optional<std::string_view>{""}
			: std::nullopt;
	}
	return is_disc_category
		? std::optional<std::string_view>{"PS3_GAME/"}
		: std::optional<std::string_view>{""};
}
}
