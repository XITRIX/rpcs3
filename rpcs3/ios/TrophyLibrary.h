#pragma once

#include "util/types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace rpcs3::ios
{
enum class trophy_grade : u32
{
	unknown = 0,
	platinum = 1,
	gold = 2,
	silver = 3,
	bronze = 4,
};

struct trophy_info
{
	std::string trophy_set_id;
	std::string game_title;
	std::string name;
	std::string description;
	std::string icon_path;
	u32 trophy_id = 0;
	u32 display_order = 0;
	trophy_grade grade = trophy_grade::unknown;
	bool earned = false;
	bool hidden = false;
	u64 unlock_timestamp = 0;
};

// Enumerates only already registered trophy data for the active PS3 user.
// This operation is read-only: it never generates or repairs TROPUSR.DAT.
std::vector<trophy_info> installed_trophies(std::string_view title_id);
}
