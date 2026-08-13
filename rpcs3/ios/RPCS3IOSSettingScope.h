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
	game_only,
};

constexpr bool setting_is_available(setting_scope scope, setting_context context) noexcept
{
	return scope == setting_scope::global_and_game || context == setting_context::game;
}
}
