#pragma once

#include <string>
#include <string_view>

enum class localized_string_id;

namespace rpcs3::ios
{
// Returns a readable, Qt-free string for native RPCS3 overlays. language_tag is
// the host's BCP-47 preferred language and is deliberately independent of the
// language exposed to emulated PlayStation 3 software.
std::string localized_overlay_string(
	localized_string_id id,
	std::string_view language_tag,
	const char* argument = "");

std::u32string localized_overlay_u32string(
	localized_string_id id,
	std::string_view language_tag,
	const char* argument = "");
}
