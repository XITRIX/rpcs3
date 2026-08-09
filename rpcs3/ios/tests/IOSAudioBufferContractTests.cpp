#include "ios/IOSAudioBufferContract.h"

#include <cassert>
#include <cstdint>
#include <limits>

int main()
{
	using rpcs3::ios::audio::aligned_written_byte_count;
	using rpcs3::ios::audio::callback_byte_count;

	static_assert(callback_byte_count(512, 8, 4096) == 4096);
	static_assert(callback_byte_count(512, 8, 2048) == 2048);
	static_assert(callback_byte_count(0, 8, 4096) == 0);
	static_assert(callback_byte_count(512, 0, 4096) == 0);
	static_assert(callback_byte_count(
		std::numeric_limits<std::uint32_t>::max(),
		std::numeric_limits<std::uint32_t>::max(),
		4096) == 4096);

	static_assert(aligned_written_byte_count(4096, 4096, 8) == 4096);
	static_assert(aligned_written_byte_count(4097, 4096, 8) == 4096);
	static_assert(aligned_written_byte_count(15, 64, 8) == 8);
	static_assert(aligned_written_byte_count(15, 64, 0) == 0);

	assert(callback_byte_count(256, 4, 1024) == 1024);
	assert(aligned_written_byte_count(1023, 1024, 4) == 1020);
	return 0;
}
