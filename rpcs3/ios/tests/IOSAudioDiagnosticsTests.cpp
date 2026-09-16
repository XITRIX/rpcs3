#include "ios/IOSAudioDiagnostics.h"
#include <cassert>
#include <cstdio>
#include <thread>

using namespace rpcs3::ios::audio_detail;

int main()
{
	callback_diagnostics callbacks;
	callbacks.record(512, 512, callback_outcome::rendered, 1000, 48000);
	callbacks.record(512, 128, callback_outcome::rendered, 11667, 48000);
	callbacks.record(256, 0, callback_outcome::lock_miss, 22334, 48000);
	callbacks.record(256, 0, callback_outcome::inactive, 50000, 48000);
	callbacks.reset_timing();
	callbacks.record(512, 0, callback_outcome::rendered, 1000000, 48000);
	callbacks.record(512, 512, callback_outcome::rendered, 1020000, 48000);
	const auto d = callbacks.snapshot();
	assert(d.callbacks == 6 && d.requested == 2560);
	assert(d.delivered == 1152 && d.filler == 1408);
	assert(d.short_reads == 2 && d.zero_reads == 1);
	assert(d.lock_misses == 1 && d.inactive == 1);
	assert(d.min_frames == 256 && d.max_frames == 512);
	assert(d.max_gap_us == 20000 && d.late_callbacks == 1);

	queue_diagnostics queue;
	queue.record_read(512, 128, {128, false, true, false, false});
	queue.record_read(512, 1000, {0, true, false, false, false});
	queue.record_read(512, 1700, {512, false, false, true, false});
	queue.record_read(512, 0, {0, false, false, true, true});
	queue.record_write(256, 0);
	queue.record_write(256, 256);
	const auto q = queue.snapshot();
	assert(q.requested == 2048 && q.delivered == 640);
	assert(q.starved_frames == 896 && q.refill_frames == 512);
	assert(q.min_fill == 0 && q.max_fill == 1700);
	assert(q.recoveries == 1 && q.resumes == 2 && q.timeouts == 1);
	assert(queue.rejected_blocks == 1 && queue.rejected_frames == 256);

	// A reader can poll without draining or losing the callback's accounting.
	callback_diagnostics concurrent;
	std::atomic_bool finished = false;
	std::thread writer([&]
	{
		for (std::uint64_t i = 0; i < 100000; ++i)
			concurrent.record(512, 256, callback_outcome::rendered, 1 + i * 10667, 48000);
		finished.store(true);
	});
	while (!finished.load()) { const auto snapshot = concurrent.snapshot(); (void)snapshot; }
	writer.join();
	const auto totals = concurrent.snapshot();
	assert(totals.callbacks == 100000 && totals.requested == 51200000);
	assert(totals.delivered == 25600000 && totals.filler == 25600000);
	assert(totals.short_reads == 100000 && totals.zero_reads == 0);
	std::puts("Audio callback/queue accounting and concurrent snapshots passed");
}
