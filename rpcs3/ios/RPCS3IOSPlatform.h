#pragma once

#include <string>
#include <string_view>

namespace rpcs3::ios
{
bool display_sleep_control_supported() noexcept;
void enable_display_sleep(bool enable) noexcept;
std::string preferred_language_identifier();
std::string localized_application_string(
	std::string_view language_tag,
	std::string_view english_value);
}
