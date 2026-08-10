#include "../RPCS3IOSInput.h"

#include <cassert>
#include <cmath>
#include <limits>

int main()
{
	using namespace rpcs3::ios;

	static_assert(RPCS3_IOS_ABI_VERSION == 12);
	static_assert(sizeof(rpcs3_ios_pad_state) == 40);
	static_assert(pad_button_mask == (UINT64_C(1) << 17) - 1);

	assert(!validate_pad_state_contract(nullptr));
	rpcs3_ios_pad_state state = disconnected_pad_state();
	assert(validate_pad_state_contract(&state));
	state.struct_size--;
	assert(!validate_pad_state_contract(&state));
	state.struct_size = sizeof(state);
	state.connected = 2;
	assert(!validate_pad_state_contract(&state));
	state.connected = 1;
	state.buttons = UINT64_C(1) << 63;
	assert(!validate_pad_state_contract(&state));
	state.buttons = RPCS3_IOS_PAD_BUTTON_CROSS | RPCS3_IOS_PAD_BUTTON_DPAD_UP;
	state.left_stick_x = -1.f;
	state.left_stick_y = 1.f;
	state.right_stick_x = 0.5f;
	state.right_stick_y = -0.5f;
	state.left_trigger = 0.25f;
	state.right_trigger = 1.f;
	assert(validate_pad_state_contract(&state));
	state.left_stick_x = 1.01f;
	assert(!validate_pad_state_contract(&state));
	state.left_stick_x = -1.f;
	state.left_trigger = -0.01f;
	assert(!validate_pad_state_contract(&state));
	state.left_trigger = std::numeric_limits<float>::quiet_NaN();
	assert(!validate_pad_state_contract(&state));
	state.left_trigger = 0.25f;

	pad_state_registry registry;
	assert(!registry.snapshot().connected);
	assert(registry.update(&state) == RPCS3_IOS_OK);
	const auto snapshot = registry.snapshot();
	assert(snapshot.connected == 1);
	assert(snapshot.buttons == state.buttons);
	assert(snapshot.left_stick_x == -1.f);
	assert(snapshot.right_trigger == 1.f);

	state.connected = 0;
	assert(registry.update(&state) == RPCS3_IOS_OK);
	const auto disconnected = registry.snapshot();
	assert(disconnected.connected == 0);
	assert(disconnected.buttons == 0);
	assert(disconnected.left_stick_x == 0.f);
	assert(disconnected.right_trigger == 0.f);

	assert(pad_button_value(RPCS3_IOS_PAD_BUTTON_CROSS, RPCS3_IOS_PAD_BUTTON_CROSS) == 255);
	assert(pad_button_value(0, RPCS3_IOS_PAD_BUTTON_CROSS) == 0);
	assert(pad_axis_negative(-1.f) == 255);
	assert(pad_axis_negative(1.f) == 0);
	assert(pad_axis_positive(1.f) == 255);
	assert(pad_axis_positive(-1.f) == 0);
	assert(pad_axis_positive(0.5f) == 128);
	assert(pad_trigger_value(0.5f, false) == 128);
	assert(pad_trigger_value(0.f, true) == 255);

	return 0;
}
