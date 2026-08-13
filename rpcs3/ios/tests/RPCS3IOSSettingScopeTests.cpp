#include "../RPCS3IOSSettingScope.h"

int main()
{
	using namespace rpcs3::ios;

	static_assert(setting_is_available(setting_scope::global_and_game, setting_context::global));
	static_assert(setting_is_available(setting_scope::global_and_game, setting_context::game));
	static_assert(!setting_is_available(setting_scope::game_only, setting_context::global));
	static_assert(setting_is_available(setting_scope::game_only, setting_context::game));

	return 0;
}
