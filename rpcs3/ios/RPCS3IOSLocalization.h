#pragma once

#include <string>
#include <string_view>

enum class localized_string_id;

namespace rpcs3::ios
{
using localization_resolver = std::string (*)(
	std::string_view language_tag,
	std::string_view english_value);

// Installs the process-lifetime host bundle lookup used by the iOS frontend.
// Passing nullptr restores the complete built-in English catalog, which keeps
// host contract tests and non-bundle callers deterministic.
void set_localization_resolver(localization_resolver resolver) noexcept;

// Returns a readable, Qt-free string for native RPCS3 overlays. language_tag is
// the app's BCP-47 preferred localization and is deliberately independent of
// the language exposed to emulated PlayStation 3 software.
std::string localized_overlay_string(
	localized_string_id id,
	std::string_view language_tag,
	const char* argument = "");

std::u32string localized_overlay_u32string(
	localized_string_id id,
	std::string_view language_tag,
	const char* argument = "");

// Localizes raw cfg enum values shown by the native Home Menu while retaining
// the RPCS3 value as the fallback and persistence representation.
std::string localized_setting_string(
	std::string_view value,
	std::string_view language_tag);
}
