#pragma once

#include "Emu/Io/PadHandler.h"
#include "ios/RPCS3IOSInput.h"

namespace rpcs3::ios
{
pad_state_registry& shared_pad_state() noexcept;
}

class ios_pad_handler final : public PadHandlerBase
{
public:
	static constexpr std::string_view device_name = "iOS Game Controller";

	ios_pad_handler();

	void init_config(cfg_pad* cfg) override;
	std::vector<pad_list_entry> list_devices() override;

private:
	enum key_code : u32
	{
		none,
		dpad_up,
		dpad_down,
		dpad_left,
		dpad_right,
		cross,
		circle,
		square,
		triangle,
		l1,
		r1,
		l2,
		r2,
		l3,
		r3,
		start,
		select,
		ps,
		left_stick_x_negative,
		left_stick_x_positive,
		left_stick_y_negative,
		left_stick_y_positive,
		right_stick_x_negative,
		right_stick_x_positive,
		right_stick_y_negative,
		right_stick_y_positive,
	};

	struct device final : PadDevice
	{
		rpcs3_ios_pad_state state = rpcs3::ios::disconnected_pad_state();
	};

	std::shared_ptr<PadDevice> get_device(const std::string& name) override;
	connection update_connection(const std::shared_ptr<PadDevice>& pad_device) override;
	std::unordered_map<u32, u16> get_button_values(const std::shared_ptr<PadDevice>& pad_device) override;
	pad_preview_values get_preview_values(
		const std::unordered_map<u32, u16>& data,
		const std::vector<std::string>& buttons) override;
	bool get_is_left_trigger(const std::shared_ptr<PadDevice>& pad_device, u32 code) override;
	bool get_is_right_trigger(const std::shared_ptr<PadDevice>& pad_device, u32 code) override;
	bool get_is_left_stick(const std::shared_ptr<PadDevice>& pad_device, u32 code) override;
	bool get_is_right_stick(const std::shared_ptr<PadDevice>& pad_device, u32 code) override;
};
