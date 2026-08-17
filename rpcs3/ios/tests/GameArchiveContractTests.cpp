#include "../GameArchiveContract.h"
#include "../GameFolderContract.h"
#include "../GamePatchContract.h"
#include "../RAPLicenseContract.h"

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

	const auto disc_folder = detect_game_folder_layout(root_disc);
	assert(disc_folder);
	assert(disc_folder->kind == game_folder_layout_kind::disc_root);
	assert(game_folder_install_prefix(disc_folder->kind, true, false) == "");
	assert(!game_folder_install_prefix(disc_folder->kind, false, true));

	const std::vector<std::string> selected_ps3_game{
		"PARAM.SFO",
		"USRDIR/EBOOT.BIN",
		"ICON0.PNG",
	};
	const auto content_folder = detect_game_folder_layout(selected_ps3_game);
	assert(content_folder);
	assert(content_folder->kind == game_folder_layout_kind::content_root);
	assert(game_folder_install_prefix(content_folder->kind, true, false) == "PS3_GAME/");
	assert(game_folder_install_prefix(content_folder->kind, false, true) == "");

	assert(!detect_game_folder_layout(missing_executable));
	std::vector<std::string> ambiguous_folder = root_disc;
	ambiguous_folder.insert(
		ambiguous_folder.end(), selected_ps3_game.begin(), selected_ps3_game.end());
	assert(!detect_game_folder_layout(ambiguous_folder));
	assert(!game_folder_install_prefix(game_folder_layout_kind::content_root, false, false));
	assert(!game_folder_install_prefix(game_folder_layout_kind::content_root, true, true));

	assert(validate_game_patch_contract("BLUS12345", "BLUS12345", "GD", true) ==
		game_patch_validation_error::none);
	assert(validate_game_patch_contract("BLUS12345", "BLES54321", "GD", true) ==
		game_patch_validation_error::title_mismatch);
	assert(validate_game_patch_contract("BLUS12345", "BLUS12345", "HG", true) ==
		game_patch_validation_error::invalid_patch);
	assert(validate_game_patch_contract("BLUS12345", "BLUS12345", "GD", false) ==
		game_patch_validation_error::invalid_patch);

	assert(normalized_rap_license_filename(
		"/private/input/EP0001-NPEB00001_00-GAMELICENSE000001.rap") ==
		"EP0001-NPEB00001_00-GAMELICENSE000001.rap");
	assert(normalized_rap_license_filename(
		"/private/input/UP0001-NPUB00001_00-GAMELICENSE000001.RAP") ==
		"UP0001-NPUB00001_00-GAMELICENSE000001.rap");
	assert(!normalized_rap_license_filename("relative/license.rap"));
	assert(!normalized_rap_license_filename("/private/input/.rap"));
	assert(!normalized_rap_license_filename("/private/input/license.pkg"));

	return 0;
}
