#pragma once

#include "RPCS3IOSGPUDefaults.h"
#include "Utilities/Config.h"

namespace rpcs3::ios
{
// Keep old configurations readable without restoring the retired UI choice.
// Deserialization covers global/title settings, presets and saved snapshots.
class shader_mode_setting final : public cfg::_enum<shader_mode>
{
public:
	using cfg::_enum<shader_mode>::_enum;

	bool from_string(std::string_view value, bool dynamic = false) override
	{
		if (!cfg::_enum<shader_mode>::from_string(value, dynamic))
		{
			return false;
		}
		if (get() == shader_mode::recompiler)
		{
			set(default_shader_mode);
		}
		return true;
	}
};
}
