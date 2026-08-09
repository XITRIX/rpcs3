#pragma once

#include <string>

namespace rpcs3::ios
{
bool display_sleep_control_supported() noexcept;
void enable_display_sleep(bool enable) noexcept;
std::string preferred_language_identifier();
}
