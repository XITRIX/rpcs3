#pragma once

#include "RPCS3IOS.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>

namespace rpcs3::ios
{
inline constexpr uint64_t pad_button_mask =
	RPCS3_IOS_PAD_BUTTON_DPAD_UP |
	RPCS3_IOS_PAD_BUTTON_DPAD_DOWN |
	RPCS3_IOS_PAD_BUTTON_DPAD_LEFT |
	RPCS3_IOS_PAD_BUTTON_DPAD_RIGHT |
	RPCS3_IOS_PAD_BUTTON_CROSS |
	RPCS3_IOS_PAD_BUTTON_CIRCLE |
	RPCS3_IOS_PAD_BUTTON_SQUARE |
	RPCS3_IOS_PAD_BUTTON_TRIANGLE |
	RPCS3_IOS_PAD_BUTTON_L1 |
	RPCS3_IOS_PAD_BUTTON_R1 |
	RPCS3_IOS_PAD_BUTTON_L2 |
	RPCS3_IOS_PAD_BUTTON_R2 |
	RPCS3_IOS_PAD_BUTTON_L3 |
	RPCS3_IOS_PAD_BUTTON_R3 |
	RPCS3_IOS_PAD_BUTTON_START |
	RPCS3_IOS_PAD_BUTTON_SELECT |
	RPCS3_IOS_PAD_BUTTON_PS;

inline bool validate_pad_state_contract(const rpcs3_ios_pad_state* state) noexcept
{
	if (!state || state->struct_size < sizeof(rpcs3_ios_pad_state) ||
		state->connected > 1 || (state->buttons & ~pad_button_mask))
	{
		return false;
	}

	const float values[] = {
		state->left_stick_x,
		state->left_stick_y,
		state->right_stick_x,
		state->right_stick_y,
		state->left_trigger,
		state->right_trigger,
	};
	for (const float value : values)
	{
		if (!std::isfinite(value))
		{
			return false;
		}
	}

	return state->left_stick_x >= -1.f && state->left_stick_x <= 1.f &&
		state->left_stick_y >= -1.f && state->left_stick_y <= 1.f &&
		state->right_stick_x >= -1.f && state->right_stick_x <= 1.f &&
		state->right_stick_y >= -1.f && state->right_stick_y <= 1.f &&
		state->left_trigger >= 0.f && state->left_trigger <= 1.f &&
		state->right_trigger >= 0.f && state->right_trigger <= 1.f;
}

inline rpcs3_ios_pad_state disconnected_pad_state() noexcept
{
	rpcs3_ios_pad_state state{};
	state.struct_size = sizeof(state);
	return state;
}

class pad_state_registry final
{
public:
	rpcs3_ios_status update(const rpcs3_ios_pad_state* state) noexcept
	{
		if (!validate_pad_state_contract(state))
		{
			return RPCS3_IOS_INVALID_ARGUMENT;
		}

		std::lock_guard lock(m_mutex);
		m_state = state->connected ? *state : disconnected_pad_state();
		m_state.struct_size = sizeof(m_state);
		return RPCS3_IOS_OK;
	}

	rpcs3_ios_pad_state snapshot() const noexcept
	{
		std::lock_guard lock(m_mutex);
		return m_state;
	}

	void clear() noexcept
	{
		std::lock_guard lock(m_mutex);
		m_state = disconnected_pad_state();
	}

private:
	mutable std::mutex m_mutex;
	rpcs3_ios_pad_state m_state = disconnected_pad_state();
};

inline uint16_t pad_button_value(uint64_t buttons, uint64_t button) noexcept
{
	return buttons & button ? 255 : 0;
}

inline uint16_t pad_axis_negative(float value) noexcept
{
	return static_cast<uint16_t>(std::clamp(-value, 0.f, 1.f) * 255.f + 0.5f);
}

inline uint16_t pad_axis_positive(float value) noexcept
{
	return static_cast<uint16_t>(std::clamp(value, 0.f, 1.f) * 255.f + 0.5f);
}

inline uint16_t pad_trigger_value(float value, bool pressed) noexcept
{
	const auto analog = static_cast<uint16_t>(std::clamp(value, 0.f, 1.f) * 255.f + 0.5f);
	return pressed ? std::max<uint16_t>(analog, 255) : analog;
}
}
