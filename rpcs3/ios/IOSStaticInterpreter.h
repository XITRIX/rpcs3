#pragma once

#include <utility>

namespace rpcs3::ios
{
// The generated SPU escape abandons the JIT stack. Native interpretation must
// instead unwind C++ frames and return to its own interpreter invocation.
struct static_interpreter_escape {};

[[noreturn]] inline void escape_static_interpreter()
{
	throw static_interpreter_escape{};
}

template <typename F>
void run_static_interpreter(F&& execute)
{
	try
	{
		std::forward<F>(execute)();
	}
	catch (const static_interpreter_escape&)
	{
		// State/PC already describe the interrupt, halt, stop or next instruction.
	}
}
}
