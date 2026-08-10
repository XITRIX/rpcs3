#pragma once

#include "Emu/system_config_types.h"

namespace rpcs3::ios
{
	inline constexpr shader_mode default_shader_mode = shader_mode::async_recompiler;
	inline constexpr bool default_precise_zcull = false;
	inline constexpr bool default_relaxed_zcull = false;
}
