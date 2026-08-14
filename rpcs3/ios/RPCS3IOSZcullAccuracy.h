#pragma once

#include <optional>
#include <string_view>

namespace rpcs3::ios
{
struct zcull_accuracy_state
{
	bool precise;
	bool relaxed;
};

constexpr std::string_view zcull_accuracy_name(zcull_accuracy_state state) noexcept
{
	if (state.relaxed)
	{
		return "Relaxed";
	}
	if (state.precise)
	{
		return "Precise";
	}
	return "Approximate";
}

constexpr std::optional<zcull_accuracy_state> parse_zcull_accuracy(std::string_view value) noexcept
{
	if (value == "Precise")
	{
		return zcull_accuracy_state{true, false};
	}
	if (value == "Approximate")
	{
		return zcull_accuracy_state{false, false};
	}
	if (value == "Relaxed")
	{
		return zcull_accuracy_state{false, true};
	}
	return std::nullopt;
}
}
