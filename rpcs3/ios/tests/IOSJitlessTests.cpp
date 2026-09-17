#include "Utilities/JITIOS.h"
#include "ios/IOSStaticInterpreter.h"

#include <cassert>
#include <cstdlib>
#include <stdexcept>

int main()
{
	using namespace rpcs3::ios;
	setenv("RPCS3_IOS_JITLESS", "1", 1);
	setenv("RPCS3_IOS_EXPANDED_JIT_ARENA", "1024", 1);
	assert(jit::is_jitless());
	assert(!jit::is_ready());
	assert(jit::prepare_arena());
	assert(jit::seal_arena());
	const auto stats = jit::get_statistics();
	assert(stats.backend == jit::arena_backend::disabled);
	assert(stats.sealed && !stats.expanded);
	assert(stats.capacity == 0 && stats.data_capacity == 0);
	assert(stats.preparation_chunks == 0);
	assert(jit::runtime_memory(true) == nullptr);
	assert(jit::runtime_memory(false) == nullptr);
	assert(jit::arena_capacity() == 0 && jit::arena_capacity(false) == 0);
	jit::reset_runtime();
	setenv("RPCS3_IOS_JITLESS", "0", 1);
	assert(jit::is_jitless()); // Mode remains fixed until process exit.

	int count = 0;
	run_static_interpreter([&] { ++count; });
	assert(count == 1);
	struct cleanup
	{
		int& count;
		~cleanup() { ++count; }
	};
	run_static_interpreter([&]
	{
		cleanup guard{count};
		escape_static_interpreter();
		assert(false);
	});
	assert(count == 2); // Native escape unwinds all C++ frames.
	run_static_interpreter([&]
	{
		run_static_interpreter([&] { escape_static_interpreter(); });
		++count; // Nested invocations catch only their own escape.
	});
	assert(count == 3);
	try
	{
		run_static_interpreter([] { throw std::runtime_error("guest failure"); });
		assert(false);
	}
	catch (const std::runtime_error&) {}
}
