#include <cassert>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

using u32 = std::uint32_t;
using u64 = std::uint64_t;
using snapshot = std::tuple<std::string, u32, u32, u64, u64, u32, u32>;

static void check(const std::vector<snapshot>& states, bool should_finish, unsigned expected_updates)
{
	u32 ftotal = 0, fdone = 0, ptotal = 0, pdone = 0;
	u64 ftotal_bits = 0, fknown_bits = 0;
	unsigned updates = 0;
	std::size_t index = 0;
	for (; index < states.size(); ++index)
	{
		const auto get_state = [&] { return states[index]; };
#include "ProgressCompletionUnderTest.inc"
		// Model the compact overlay's early continue. Completion must exit
		// before this path, retaining the observed counters for subtraction.
		++updates;
		continue;
	}
	assert(updates == expected_updates);
	assert((index < states.size()) == should_finish);
	if (!should_finish) return;

	auto [text, remaining_files, done_files, total_bits, known_bits, remaining_modules, done_modules] = states[index];
	assert(text.empty());
	remaining_files -= ftotal;
	done_files -= fdone;
	total_bits -= ftotal_bits;
	known_bits -= fknown_bits;
	remaining_modules -= ptotal;
	done_modules -= pdone;
	assert(!remaining_files && !done_files && !total_bits && !known_bits && !remaining_modules && !done_modules);
}

int main()
{
	const snapshot finished{"", 0, 0, 0, 0, 7, 7};
	// Exact reported 7-of-7 case, including finishing before the first UI update.
	check({finished}, true, 0);
	check({{"Compiling PPU modules", 0, 0, 0, 0, 7, 6}, finished}, true, 1);
	// 100% with an active linking/apply stage is not completion.
	check({{"Linking PPU modules", 0, 0, 0, 0, 7, 7}}, false, 1);
	check({{"", 0, 0, 0, 0, 7, 6}}, false, 1);
	check({{"", 2, 1, 100, 50, 7, 7}}, false, 1);
	// File and module counters must both finish, including byte-weighted batches.
	check({{"", 2, 2, 100, 100, 7, 7}}, true, 0);
	check({{"", 0, 0, 0, 0, 0, 0}}, true, 0);
	// A new batch between observations must keep the progress server working.
	check({{"Applying PPU code", 0, 0, 0, 0, 7, 7},
		{"Compiling SPU modules", 0, 0, 0, 0, 9, 7},
		{"", 0, 0, 0, 0, 9, 9}}, true, 2);
}
