#include "../RPCS3IOSBootProgress.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <future>
#include <stdexcept>
#include <thread>

// Exercise the production snapshot reader without the emulator/LLVM runtime.
// The test runner extracts it unchanged from RPCS3IOS.cpp. Native compilation
// counters deliberately retain activity while a renderer stage is published.
using u32 = uint32_t;
using u64 = uint64_t;
struct native_progress_text
{
	operator std::string() const { return "Compiling title modules"; }
} g_progr_text;
u32 g_progr_ftotal = 2, g_progr_fdone = 1;
u64 g_progr_ftotal_bits = 100, g_progr_fknown_bits = 50;
u32 g_progr_ptotal = 200, g_progr_pdone = 90;

#include "BootProgressCaptureUnderTest.h"

int main()
{
	using namespace rpcs3::ios;
	using namespace std::chrono_literals;
	auto& registry = boot_stages();
	assert(registry.snapshot().empty());

	// Simulate a boot holding its lifecycle lock inside a blocking driver call.
	// A reader must see the stage without requiring either lock or completion.
	std::mutex lifecycle;
	std::promise<void> entered, release;
	auto release_future = release.get_future();
	std::thread boot([&]
	{
		std::lock_guard lifecycle_lock(lifecycle);
		scoped_boot_stage stage{"Preparing graphics cache"};
		entered.set_value();
		release_future.wait();
	});
	entered.get_future().wait();
	auto observation = std::async(std::launch::async, [] { return capture_boot_progress(); });
	assert(observation.wait_for(1s) == std::future_status::ready);
	const auto observed = observation.get();
	assert(observed.text == "Preparing graphics cache");
	assert(!observed.files_total && !observed.files_done);
	assert(!observed.modules_total && !observed.modules_done);
	assert(!observed.file_bits_total && !observed.file_bits_known);
	release.set_value();
	boot.join();
	assert(registry.snapshot().empty());

	// The real native compilation snapshot resumes after the scoped override.
	const auto native = capture_boot_progress();
	assert(native.text == "Compiling title modules");
	assert(native.files_total == 2 && native.files_done == 1);
	assert(native.modules_total == 200 && native.modules_done == 90);
	assert(native.file_bits_total == 100 && native.file_bits_known == 50);

	// Failure, early return and a fresh-cache retry must not leak a stage into
	// the next boot or conceal a later native compilation phase.
	try
	{
		scoped_boot_stage stage{"Reading graphics cache"};
		stage.update("Preparing graphics cache");
		throw std::runtime_error("simulated driver exception");
	}
	catch (const std::runtime_error&) {}
	assert(registry.snapshot().empty());
	const auto retry = [&]
	{
		scoped_boot_stage stage{"Preparing graphics cache"};
		stage.update("Preparing a fresh graphics cache");
		assert(registry.snapshot() == "Preparing a fresh graphics cache");
		return;
	};
	retry();
	assert(registry.snapshot().empty());

	// Concurrent observation must never return a torn or partially updated
	// string, including scopes clearing while the reader takes a snapshot.
	const std::string long_stage(300, 'x');
	std::atomic<bool> finished = false;
	std::thread writer([&]
	{
		for (int i = 0; i < 10000; ++i)
		{
			scoped_boot_stage stage{"Reading graphics cache"};
			stage.update(long_stage);
		}
		finished = true;
	});
	do
	{
		const auto value = registry.snapshot();
		assert(value.empty() || value == "Reading graphics cache" || value == long_stage);
	} while (!finished);
	writer.join();
	assert(registry.snapshot().empty());
}
