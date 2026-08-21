#include "../RPCS3IOSPath.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>

int main()
{
	using rpcs3::ios::normalize_resolved_host_path;
	using rpcs3::ios::is_lexically_within_path;
	using rpcs3::ios::is_resolved_within_path;

	assert(normalize_resolved_host_path("").empty());
	assert(normalize_resolved_host_path("/") == "/");
	assert(normalize_resolved_host_path("/dev_hdd0/game") == "/dev_hdd0/game");
	assert(normalize_resolved_host_path("/dev_hdd0/game///") == "/dev_hdd0/game");

	const std::string hdd0_game = "/sandbox/dev_hdd0/game/";
	const std::string update_boot = hdd0_game + "BLES00635/USRDIR/EBOOT.BIN";
	const std::string resolved_hdd0 = normalize_resolved_host_path(hdd0_game) + '/';
	const std::string update_tail = update_boot.substr(resolved_hdd0.size());

	assert(update_tail == "BLES00635/USRDIR/EBOOT.BIN");
	assert(update_tail.starts_with("BLES00635/"));

	assert(is_lexically_within_path(
		"/sandbox/Library/Caches/RPCS3",
		"/sandbox/Library/Caches/RPCS3/UpdateDownloads/job/Update.pkg"));
	assert(is_lexically_within_path(
		"/sandbox/Library/Caches/RPCS3/",
		"/sandbox/Library/Caches/RPCS3/UpdateDownloads/Update.pkg"));
	assert(!is_lexically_within_path(
		"/sandbox/Library/Caches/RPCS3",
		"/sandbox/Library/Caches/RPCS3-other/Update.pkg"));
	assert(!is_lexically_within_path(
		"/sandbox/Library/Caches/RPCS3",
		"/sandbox/Library/Caches/RPCS3/../Documents/Update.pkg"));
	assert(!is_lexically_within_path("relative", "/sandbox/Update.pkg"));
	assert(!is_lexically_within_path("/sandbox", "relative/Update.pkg"));

#ifndef _WIN32
	const std::filesystem::path test_root =
		std::filesystem::temp_directory_path() /
		("rpcs3-ios-path-" + std::to_string(
			std::chrono::steady_clock::now().time_since_epoch().count()));
	const std::filesystem::path cache = test_root / "cache";
	const std::filesystem::path outside = test_root / "outside";
	std::error_code error;
	assert(std::filesystem::create_directories(cache / "imports", error) && !error);
	assert(std::filesystem::create_directories(outside, error) && !error);
	assert(is_resolved_within_path(
		cache.string(), (cache / "imports" / "Preset.yml").string()));
	std::filesystem::create_directory_symlink(outside, cache / "escape", error);
	assert(!error);
	assert(!is_resolved_within_path(
		cache.string(), (cache / "escape" / "Preset.yml").string()));
	std::filesystem::remove_all(test_root, error);
	assert(!error);
#endif

	return 0;
}
