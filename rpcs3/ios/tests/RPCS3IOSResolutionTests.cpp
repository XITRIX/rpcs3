#include "../RPCS3IOSResolution.h"

#include <cassert>
#include <string>
#include <vector>

int main()
{
	using namespace rpcs3::ios;
	const std::vector<std::string> progressive{
		"1920x1080", "1280x720", "720x480", "720x576",
		"1600x1080", "1440x1080", "1280x1080", "960x1080",
	};

	{
		auto options = progressive;
		filter_game_resolution_options(options, psf::resolution_flag::_720);
		assert((options == std::vector<std::string>{"1280x720"}));
	}
	{
		auto options = progressive;
		filter_game_resolution_options(options, psf::resolution_flag::_1080);
		assert((options == std::vector<std::string>{
			"1920x1080", "1600x1080", "1440x1080", "1280x1080", "960x1080",
		}));
	}
	{
		auto options = progressive;
		filter_game_resolution_options(options, psf::resolution_flag::_480_16_9);
		assert((options == std::vector<std::string>{"720x480"}));
	}
	{
		auto options = progressive;
		filter_game_resolution_options(options, 1u << 20);
		assert((options == std::vector<std::string>{"1280x720"}));
	}
	{
		std::vector<std::string> options{"1920x1080i", "1280x720", "720x480i"};
		filter_game_resolution_options(options, 0);
		assert((options == std::vector<std::string>{"1280x720"}));
	}

	assert(game_supports_resolution(psf::resolution_flag::_720, "1280x720"));
	assert(!game_supports_resolution(psf::resolution_flag::_720, "1920x1080"));
	assert(game_supports_resolution(0, "1920x1080"));
	return 0;
}
