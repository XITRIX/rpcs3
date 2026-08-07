#pragma once

namespace rpcs3::ios
{
struct capabilities final
{
	static constexpr bool qt = false;
	static constexpr bool vulkan = false;
	static constexpr bool audio_output = false;
	static constexpr bool physical_input = false;
	static constexpr bool camera = false;
	static constexpr bool microphone = false;
	static constexpr bool music = false;
	static constexpr bool media_codecs = false;
	static constexpr bool usb_passthrough = false;
	static constexpr bool hid_passthrough = false;
	static constexpr bool ps_move = false;
	static constexpr bool raw_sockets = false;
	static constexpr bool upnp = false;
	static constexpr bool desktop_integration = false;
	static constexpr bool updater = false;
	static constexpr bool llvm_jit = true;
};
}
