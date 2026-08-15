#pragma once

namespace rpcs3::ios
{
enum class setting_context
{
	global,
	game,
};

enum class setting_scope
{
	global_and_game,
	global_only,
	game_only,
};

constexpr bool setting_is_available(setting_scope scope, setting_context context) noexcept
{
	if (scope == setting_scope::global_and_game)
	{
		return true;
	}

	return scope == setting_scope::global_only
		? context == setting_context::global
		: context == setting_context::game;
}
}
