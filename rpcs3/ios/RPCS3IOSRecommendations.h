#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace rpcs3::ios
{
struct title_recommendations
{
	std::string config;
	// Explicit port recommendations remain visible even when equal to defaults.
	std::vector<std::string_view> port_keys;
};

// RPCS3 database first, then narrowly scoped Apple-port recommendations.
// This only constructs recommendations; custom configurations retain precedence.
title_recommendations recommendations_for_title(
	std::string_view title_id, std::string_view installed_version);
}
