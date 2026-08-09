#include "stdafx.h"
#include "IOSPadHandler.h"

#include "Emu/Io/pad_config.h"

namespace rpcs3::ios
{
pad_state_registry& shared_pad_state() noexcept
{
	static pad_state_registry registry;
	return registry;
}
}

ios_pad_handler::ios_pad_handler()
	: PadHandlerBase(pad_handler::ios)
{
	button_list =
	{
		{none, ""},
		{dpad_up, "D-Pad Up"},
		{dpad_down, "D-Pad Down"},
		{dpad_left, "D-Pad Left"},
		{dpad_right, "D-Pad Right"},
		{cross, "Cross"},
		{circle, "Circle"},
		{square, "Square"},
		{triangle, "Triangle"},
		{l1, "L1"},
		{r1, "R1"},
		{l2, "L2"},
		{r2, "R2"},
		{l3, "L3"},
		{r3, "R3"},
		{start, "Start"},
		{select, "Select"},
		{ps, "PS"},
		{left_stick_x_negative, "Left Stick X-"},
		{left_stick_x_positive, "Left Stick X+"},
		{left_stick_y_negative, "Left Stick Y-"},
		{left_stick_y_positive, "Left Stick Y+"},
		{right_stick_x_negative, "Right Stick X-"},
		{right_stick_x_positive, "Right Stick X+"},
		{right_stick_y_negative, "Right Stick Y-"},
		{right_stick_y_positive, "Right Stick Y+"},
	};

	thumb_max = 255;
	trigger_min = 0;
	trigger_max = 255;
	m_thumb_threshold = thumb_max / 2;
	m_trigger_threshold = trigger_max / 2;
	m_name_string = std::string{device_name};
	m_max_devices = 1;
	b_has_deadzones = true;
	b_has_pressure_intensity_button = false;
	b_has_analog_limiter_button = false;
	b_has_orientation = false;
	b_has_rumble = false;
	init_configs();
}

void ios_pad_handler::init_config(cfg_pad* cfg)
{
	if (!cfg)
	{
		return;
	}

	cfg->ls_left.def = ::at32(button_list, left_stick_x_negative);
	cfg->ls_down.def = ::at32(button_list, left_stick_y_negative);
	cfg->ls_right.def = ::at32(button_list, left_stick_x_positive);
	cfg->ls_up.def = ::at32(button_list, left_stick_y_positive);
	cfg->rs_left.def = ::at32(button_list, right_stick_x_negative);
	cfg->rs_down.def = ::at32(button_list, right_stick_y_negative);
	cfg->rs_right.def = ::at32(button_list, right_stick_x_positive);
	cfg->rs_up.def = ::at32(button_list, right_stick_y_positive);
	cfg->start.def = ::at32(button_list, start);
	cfg->select.def = ::at32(button_list, select);
	cfg->ps.def = ::at32(button_list, ps);
	cfg->square.def = ::at32(button_list, square);
	cfg->cross.def = ::at32(button_list, cross);
	cfg->circle.def = ::at32(button_list, circle);
	cfg->triangle.def = ::at32(button_list, triangle);
	cfg->left.def = ::at32(button_list, dpad_left);
	cfg->down.def = ::at32(button_list, dpad_down);
	cfg->right.def = ::at32(button_list, dpad_right);
	cfg->up.def = ::at32(button_list, dpad_up);
	cfg->r1.def = ::at32(button_list, r1);
	cfg->r2.def = ::at32(button_list, r2);
	cfg->r3.def = ::at32(button_list, r3);
	cfg->l1.def = ::at32(button_list, l1);
	cfg->l2.def = ::at32(button_list, l2);
	cfg->l3.def = ::at32(button_list, l3);
	cfg->pressure_intensity_button.def = "";
	cfg->analog_limiter_button.def = "";
	cfg->orientation_reset_button.def = "";
	cfg->lstick_anti_deadzone.def = 0;
	cfg->rstick_anti_deadzone.def = 0;
	cfg->lstickdeadzone.def = 20;
	cfg->rstickdeadzone.def = 20;
	cfg->ltriggerthreshold.def = 8;
	cfg->rtriggerthreshold.def = 8;
	cfg->from_default();
}

std::vector<pad_list_entry> ios_pad_handler::list_devices()
{
	return {pad_list_entry{std::string{device_name}, false}};
}

std::shared_ptr<PadDevice> ios_pad_handler::get_device(const std::string& name)
{
	return name == device_name ? std::make_shared<device>() : nullptr;
}

PadHandlerBase::connection ios_pad_handler::update_connection(const std::shared_ptr<PadDevice>& pad_device)
{
	auto* ios_device = static_cast<device*>(pad_device.get());
	if (!ios_device)
	{
		return connection::disconnected;
	}

	ios_device->state = rpcs3::ios::shared_pad_state().snapshot();
	return ios_device->state.connected ? connection::connected : connection::disconnected;
}

std::unordered_map<u32, u16> ios_pad_handler::get_button_values(const std::shared_ptr<PadDevice>& pad_device)
{
	std::unordered_map<u32, u16> values;
	const auto* ios_device = static_cast<const device*>(pad_device.get());
	if (!ios_device || !ios_device->state.connected)
	{
		return values;
	}

	const auto& state = ios_device->state;
	const auto button = [&state](uint64_t mask)
	{
		return rpcs3::ios::pad_button_value(state.buttons, mask);
	};
	values[dpad_up] = button(RPCS3_IOS_PAD_BUTTON_DPAD_UP);
	values[dpad_down] = button(RPCS3_IOS_PAD_BUTTON_DPAD_DOWN);
	values[dpad_left] = button(RPCS3_IOS_PAD_BUTTON_DPAD_LEFT);
	values[dpad_right] = button(RPCS3_IOS_PAD_BUTTON_DPAD_RIGHT);
	values[cross] = button(RPCS3_IOS_PAD_BUTTON_CROSS);
	values[circle] = button(RPCS3_IOS_PAD_BUTTON_CIRCLE);
	values[square] = button(RPCS3_IOS_PAD_BUTTON_SQUARE);
	values[triangle] = button(RPCS3_IOS_PAD_BUTTON_TRIANGLE);
	values[l1] = button(RPCS3_IOS_PAD_BUTTON_L1);
	values[r1] = button(RPCS3_IOS_PAD_BUTTON_R1);
	values[l2] = rpcs3::ios::pad_trigger_value(
		state.left_trigger, state.buttons & RPCS3_IOS_PAD_BUTTON_L2);
	values[r2] = rpcs3::ios::pad_trigger_value(
		state.right_trigger, state.buttons & RPCS3_IOS_PAD_BUTTON_R2);
	values[l3] = button(RPCS3_IOS_PAD_BUTTON_L3);
	values[r3] = button(RPCS3_IOS_PAD_BUTTON_R3);
	values[start] = button(RPCS3_IOS_PAD_BUTTON_START);
	values[select] = button(RPCS3_IOS_PAD_BUTTON_SELECT);
	values[ps] = button(RPCS3_IOS_PAD_BUTTON_PS);
	values[left_stick_x_negative] = rpcs3::ios::pad_axis_negative(state.left_stick_x);
	values[left_stick_x_positive] = rpcs3::ios::pad_axis_positive(state.left_stick_x);
	values[left_stick_y_negative] = rpcs3::ios::pad_axis_negative(state.left_stick_y);
	values[left_stick_y_positive] = rpcs3::ios::pad_axis_positive(state.left_stick_y);
	values[right_stick_x_negative] = rpcs3::ios::pad_axis_negative(state.right_stick_x);
	values[right_stick_x_positive] = rpcs3::ios::pad_axis_positive(state.right_stick_x);
	values[right_stick_y_negative] = rpcs3::ios::pad_axis_negative(state.right_stick_y);
	values[right_stick_y_positive] = rpcs3::ios::pad_axis_positive(state.right_stick_y);
	return values;
}

pad_preview_values ios_pad_handler::get_preview_values(
	const std::unordered_map<u32, u16>& data,
	const std::vector<std::string>&)
{
	return {
		::at32(data, l2),
		::at32(data, r2),
		::at32(data, left_stick_x_positive) - ::at32(data, left_stick_x_negative),
		::at32(data, left_stick_y_positive) - ::at32(data, left_stick_y_negative),
		::at32(data, right_stick_x_positive) - ::at32(data, right_stick_x_negative),
		::at32(data, right_stick_y_positive) - ::at32(data, right_stick_y_negative),
	};
}

bool ios_pad_handler::get_is_left_trigger(const std::shared_ptr<PadDevice>&, u32 code)
{
	return code == l2;
}

bool ios_pad_handler::get_is_right_trigger(const std::shared_ptr<PadDevice>&, u32 code)
{
	return code == r2;
}

bool ios_pad_handler::get_is_left_stick(const std::shared_ptr<PadDevice>&, u32 code)
{
	return code >= left_stick_x_negative && code <= left_stick_y_positive;
}

bool ios_pad_handler::get_is_right_stick(const std::shared_ptr<PadDevice>&, u32 code)
{
	return code >= right_stick_x_negative && code <= right_stick_y_positive;
}
