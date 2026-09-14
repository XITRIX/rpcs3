#include "../IOSGraphicsLifecycle.h"

#include <cassert>
#include <chrono>
#include <future>
#include <thread>

using namespace std::chrono_literals;

int main()
{
	rpcs3::ios::graphics_lifecycle state;
	std::atomic<unsigned> drains = 0;
	state.register_device(&drains, [](void* ptr) { ++*static_cast<std::atomic<unsigned>*>(ptr); });
	const auto initial_generation = state.generation();

	// A native frame admitted before UIKit's transition must finish, including
	// its asynchronous offloader submit, before the device is drained.
	std::promise<void> frame_started, release_frame;
	auto release = release_frame.get_future();
	auto frame = std::async(std::launch::async, [&]
	{
		auto scope = state.try_begin_frame();
		assert(scope);
		frame_started.set_value();
		release.wait();
	});
	frame_started.get_future().wait();
	auto deactivate = std::async(std::launch::async, [&] { state.set_active(false); });
	while (state.active()) std::this_thread::yield();
	assert(!state.try_begin_frame());
	assert(drains == 0);
	auto offloader = std::async(std::launch::async, [&] { auto scope = state.begin_submission(); });
	assert(offloader.wait_for(2s) == std::future_status::ready);
	offloader.get();
	release_frame.set_value();
	frame.get();
	assert(deactivate.wait_for(2s) == std::future_status::ready);
	deactivate.get();
	assert(drains == 1);

	// Recorded GPU work is retained and sleeps until foreground activation;
	// independent CPU preparation and progress reporting can still run.
	std::promise<void> submit_started;
	auto deferred_submit = std::async(std::launch::async, [&]
	{
		submit_started.set_value();
		auto scope = state.begin_submission();
		return 42;
	});
	submit_started.get_future().wait();
	assert(deferred_submit.wait_for(20ms) == std::future_status::timeout);
	unsigned compiled = 0;
	for (unsigned i = 0; i < 100; ++i) { assert(!state.try_begin_frame()); ++compiled; }
	assert(compiled == 100);
	state.set_active(true);
	assert(deferred_submit.wait_for(2s) == std::future_status::ready);
	assert(deferred_submit.get() == 42);
	assert(state.generation() == initial_generation + 1);
	state.set_active(true);
	assert(state.generation() == initial_generation + 1);

	// The drain must also wait for a submission outside a display-frame scope.
	std::promise<void> submit_acquired, release_submit;
	auto release_gpu = release_submit.get_future();
	auto in_flight = std::async(std::launch::async, [&]
	{
		auto scope = state.begin_submission();
		submit_acquired.set_value();
		release_gpu.wait();
	});
	submit_acquired.get_future().wait();
	auto second_deactivate = std::async(std::launch::async, [&] { state.set_active(false); });
	while (state.active()) std::this_thread::yield();
	assert(drains == 1);
	assert(second_deactivate.wait_for(20ms) == std::future_status::timeout);
	release_submit.set_value();
	in_flight.get();
	second_deactivate.get();
	assert(drains == 2);
	state.unregister_device(&drains);
	state.set_active(true);
	state.set_active(false);
	assert(drains == 2);
	state.set_active(true);
	assert(state.try_begin_frame());
}
