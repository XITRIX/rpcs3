#include "../GameArchiveContract.h"

#include <cassert>
#include <string>
#include <vector>

int main()
{
	using namespace rpcs3::ios;

	assert(normalize_game_archive_path("Game/PS3_GAME/PARAM.SFO", false));
	assert(normalize_game_archive_path("Game/PS3_GAME/", true) == "Game/PS3_GAME");
	assert(!normalize_game_archive_path("../PARAM.SFO", false));
	assert(!normalize_game_archive_path("Game/../PARAM.SFO", false));
	assert(!normalize_game_archive_path("/Game/PARAM.SFO", false));
	assert(!normalize_game_archive_path("C:/Game/PARAM.SFO", false));
	assert(!normalize_game_archive_path("Game\\PARAM.SFO", false));
	assert(!normalize_game_archive_path("Game//PARAM.SFO", false));

	const std::vector<std::string> root_disc{
		"PS3_GAME/PARAM.SFO",
		"PS3_GAME/USRDIR/EBOOT.BIN",
		"PS3_GAME/ICON0.PNG",
	};
	const auto root_disc_layout = detect_game_archive_layout(root_disc);
	assert(root_disc_layout);
	assert(root_disc_layout->kind == game_archive_layout_kind::disc_folder);
	assert(root_disc_layout->prefix.empty());

	const std::vector<std::string> wrapped_disc{
		"My Game/PS3_GAME/PARAM.SFO",
		"My Game/PS3_GAME/USRDIR/EBOOT.BIN",
	};
	const auto wrapped_disc_layout = detect_game_archive_layout(wrapped_disc);
	assert(wrapped_disc_layout);
	assert(wrapped_disc_layout->prefix == "My Game/");

	const std::vector<std::string> hdd_game{
		"NPXX00001/PARAM.SFO",
		"NPXX00001/USRDIR/EBOOT.BIN",
	};
	const auto hdd_layout = detect_game_archive_layout(hdd_game);
	assert(hdd_layout);
	assert(hdd_layout->kind == game_archive_layout_kind::hdd_folder);
	assert(hdd_layout->prefix == "NPXX00001/");

	const std::vector<std::string> missing_executable{"PS3_GAME/PARAM.SFO"};
	assert(!detect_game_archive_layout(missing_executable));

	std::vector<std::string> multiple_games = wrapped_disc;
	multiple_games.insert(multiple_games.end(), hdd_game.begin(), hdd_game.end());
	assert(!detect_game_archive_layout(multiple_games));

	return 0;
}
