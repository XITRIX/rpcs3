#pragma once

#include "RPCS3IOS.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>

namespace rpcs3::ios
{
inline constexpr uint32_t pad_player_count = 7;

inline bool validate_pad_player_index(uint32_t player_index) noexcept
{
	return player_index < pad_player_count;
}

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

class pad_feedback_registry final
{
public:
	void update(uint8_t large_motor, uint8_t small_motor) noexcept
	{
		const uint16_t packed = static_cast<uint16_t>(large_motor) |
			(static_cast<uint16_t>(small_motor) << 8);
		m_motors.store(packed, std::memory_order_release);
	}

	rpcs3_ios_pad_feedback snapshot() const noexcept
	{
		const uint16_t packed = m_motors.load(std::memory_order_acquire);
		rpcs3_ios_pad_feedback feedback{};
		feedback.struct_size = sizeof(feedback);
		feedback.large_motor = packed & 0xff;
		feedback.small_motor = packed >> 8;
		return feedback;
	}

	void clear() noexcept
	{
		m_motors.store(0, std::memory_order_release);
	}

private:
	std::atomic<uint16_t> m_motors{0};
};

class pad_state_registries final
{
public:
	rpcs3_ios_status update(
		uint32_t player_index,
		const rpcs3_ios_pad_state* state) noexcept
	{
		if (!validate_pad_player_index(player_index))
		{
			return RPCS3_IOS_INVALID_ARGUMENT;
		}
		return m_registries[player_index].update(state);
	}

	rpcs3_ios_pad_state snapshot(uint32_t player_index) const noexcept
	{
		return validate_pad_player_index(player_index)
			? m_registries[player_index].snapshot()
			: disconnected_pad_state();
	}

	void clear() noexcept
	{
		for (auto& registry : m_registries)
		{
			registry.clear();
		}
	}

private:
	std::array<pad_state_registry, pad_player_count> m_registries;
};

class pad_feedback_registries final
{
public:
	void update(uint32_t player_index, uint8_t large_motor, uint8_t small_motor) noexcept
	{
		if (validate_pad_player_index(player_index))
		{
			m_registries[player_index].update(large_motor, small_motor);
		}
	}

	rpcs3_ios_pad_feedback snapshot(uint32_t player_index) const noexcept
	{
		return validate_pad_player_index(player_index)
			? m_registries[player_index].snapshot()
			: rpcs3_ios_pad_feedback{sizeof(rpcs3_ios_pad_feedback), 0, 0, 0};
	}

	void clear(uint32_t player_index) noexcept
	{
		if (validate_pad_player_index(player_index))
		{
			m_registries[player_index].clear();
		}
	}

	void clear() noexcept
	{
		for (auto& registry : m_registries)
		{
			registry.clear();
		}
	}

private:
	std::array<pad_feedback_registry, pad_player_count> m_registries;
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
