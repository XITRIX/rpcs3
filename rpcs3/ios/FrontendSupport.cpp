#include "stdafx.h"

#include "Emu/Cell/SPURecompiler.h"
#include "Emu/Io/pad_config.h"
#include "Input/ps_move_config.h"
#include "Utilities/Thread.h"

#include <chrono>
#include <functional>
#include <string>
#include <thread>

// These objects normally live in the Qt executable.  The iOS frontend owns
// them instead so the emulator library has no desktop-frontend dependency.
std::string g_input_config_override;
cfg_input_configurations g_cfg_input_configs;

void qt_events_aware_op(int repeat_duration_ms, std::function<bool()> wrapped_op)
{
	ensure(wrapped_op);

	while (!wrapped_op())
	{
		if (repeat_duration_ms == 0)
		{
			std::this_thread::yield();
		}
		else if (thread_ctrl::get_current())
		{
			thread_ctrl::wait_for(repeat_duration_ms * 1000);
		}
		else
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(repeat_duration_ms));
		}
	}
}

#ifndef LLVM_AVAILABLE
// A non-LLVM build is useful for compile/link smoke tests.  Production iOS
// builds define LLVM_AVAILABLE and use the real implementation.
void spu_llvm_set_compile_context(spu_llvm_compile_context*) noexcept
{
}
#endif
