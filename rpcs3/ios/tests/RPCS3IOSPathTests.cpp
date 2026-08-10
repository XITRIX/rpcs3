#include "../RPCS3IOSPath.h"

#include <cassert>
#include <string>

int main()
{
	using rpcs3::ios::normalize_resolved_host_path;

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

	return 0;
}
