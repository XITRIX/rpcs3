#include "stdafx.h"
#include "TrophyLibrary.h"
#include "GameLibrary.h"

#include "Emu/System.h"
#include "Emu/VFS.h"
#include "Emu/system_utils.hpp"
#include "Loader/TROPUSR.h"
#include "Utilities/File.h"
#include "Utilities/StrFmt.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <unordered_set>

namespace rpcs3::ios
{
namespace
{
struct trophy_configuration
{
	std::string game_title;
	std::shared_ptr<rXmlNode> root;
	trophy_xml_document document;
};

std::string normalized_title(std::string_view title)
{
	std::string normalized;
	normalized.reserve(title.size());
	for (const unsigned char character : title)
	{
		if (character >= 'A' && character <= 'Z')
		{
			normalized.push_back(static_cast<char>(character - 'A' + 'a'));
		}
		else if ((character >= 'a' && character <= 'z') ||
			(character >= '0' && character <= '9'))
		{
			normalized.push_back(static_cast<char>(character));
		}
	}
	return normalized;
}

std::unique_ptr<trophy_configuration> read_configuration(const std::string& path)
{
	fs::file file{path};
	if (!file)
	{
		return nullptr;
	}

	auto configuration = std::make_unique<trophy_configuration>();
	if (!configuration->document.Read(file.to_string()))
	{
		return nullptr;
	}
	configuration->root = configuration->document.GetRoot();
	if (!configuration->root)
	{
		return nullptr;
	}

	for (auto node = configuration->root->GetChildren(); node; node = node->GetNext())
	{
		if (node->GetName() == "title-name")
		{
			configuration->game_title = node->GetNodeContent();
			break;
		}
	}
	return configuration;
}

void append_source_trophy_sets(
	const std::string& trophy_directory,
	std::unordered_set<std::string>& result)
{
	for (auto&& entry : fs::dir{trophy_directory})
	{
		if (!entry.is_directory || entry.name == "." || entry.name == "..")
		{
			continue;
		}
		if (fs::is_file(trophy_directory + entry.name + "/TROPHY.TRP"))
		{
			result.emplace(entry.name);
		}
	}
}

std::unordered_set<std::string> source_trophy_sets(const installed_game& game)
{
	std::unordered_set<std::string> result;
	append_source_trophy_sets(
		rpcs3::utils::get_hdd0_game_dir() + game.title_id + "/TROPDIR/", result);

	if (fs::is_dir(game.path))
	{
		const std::string content_root = fs::is_dir(game.path + "/PS3_GAME")
			? game.path + "/PS3_GAME"
			: game.path;
		append_source_trophy_sets(content_root + "/TROPDIR/", result);
	}
	return result;
}

trophy_grade parsed_grade(u32 value)
{
	switch (value)
	{
	case 1: return trophy_grade::platinum;
	case 2: return trophy_grade::gold;
	case 3: return trophy_grade::silver;
	case 4: return trophy_grade::bronze;
	default: return trophy_grade::unknown;
	}
}
}

std::vector<trophy_info> installed_trophies(std::string_view title_id)
{
	std::vector<trophy_info> result;
	const auto game = find_installed_game(title_id);
	if (!game)
	{
		return result;
	}

	const std::unordered_set<std::string> expected_sets = source_trophy_sets(*game);
	const std::string expected_title = normalized_title(game->title);
	const std::string trophy_vfs_root = "/dev_hdd0/home/" + Emu.GetUsr() + "/trophy/";
	const std::string trophy_root = vfs::get(trophy_vfs_root);

	for (auto&& entry : fs::dir{trophy_root})
	{
		if (!entry.is_directory || entry.name == "." || entry.name == "..")
		{
			continue;
		}

		const std::string physical_set_path = trophy_root + entry.name;
		auto configuration = read_configuration(physical_set_path + "/TROPCONF.SFM");
		if (!configuration)
		{
			continue;
		}

		const bool source_match = expected_sets.contains(entry.name);
		const bool title_match = expected_sets.empty() && !expected_title.empty() &&
			normalized_title(configuration->game_title) == expected_title;
		if (!source_match && !title_match)
		{
			continue;
		}

		TROPUSRLoader user_trophies;
		if (!user_trophies.LoadExisting(trophy_vfs_root + entry.name + "/TROPUSR.DAT"))
		{
			continue;
		}

		u32 display_order = 0;
		for (auto node = configuration->root->GetChildren(); node; node = node->GetNext())
		{
			if (node->GetName() != "trophy")
			{
				continue;
			}

			const std::string id_text = node->GetAttribute("id");
			if (id_text.empty())
			{
				continue;
			}
			const u32 trophy_id = static_cast<u32>(std::strtoul(id_text.c_str(), nullptr, 10));
			if (trophy_id >= user_trophies.GetTrophiesCount())
			{
				continue;
			}

			std::string name;
			std::string description;
			for (auto child = node->GetChildren(); child; child = child->GetNext())
			{
				if (child->GetName() == "name")
				{
					name = child->GetNodeContent();
				}
				else if (child->GetName() == "detail")
				{
					description = child->GetNodeContent();
				}
			}
			if (name.empty())
			{
				name = fmt::format("Trophy %u", trophy_id);
			}

			std::string icon_path = physical_set_path + fmt::format("/TROP%03d.PNG", trophy_id);
			if (!fs::is_file(icon_path))
			{
				icon_path.clear();
			}
			result.push_back({
				entry.name,
				configuration->game_title.empty() ? game->title : configuration->game_title,
				std::move(name),
				std::move(description),
				std::move(icon_path),
				trophy_id,
				display_order++,
				parsed_grade(user_trophies.GetTrophyGrade(trophy_id)),
				user_trophies.GetTrophyUnlockState(trophy_id) != 0,
				node->GetAttribute("hidden") == "y",
				user_trophies.GetTrophyTimestamp(trophy_id),
			});
		}
	}

	std::sort(result.begin(), result.end(), [](const trophy_info& left, const trophy_info& right)
	{
		return left.trophy_set_id == right.trophy_set_id
			? left.display_order < right.display_order
			: left.trophy_set_id < right.trophy_set_id;
	});
	return result;
}
}
