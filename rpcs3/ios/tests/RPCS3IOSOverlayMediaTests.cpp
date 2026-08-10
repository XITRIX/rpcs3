#include "ios/RPCS3IOSOverlayMediaBuffer.h"

#include <array>
#include <cassert>

int main()
{
	rpcs3::ios::overlay_pcm_buffer buffer{6};
	assert(buffer.capacity() == 6);
	assert(buffer.size() == 0);

	const std::array first{1.0f, 2.0f, 3.0f, 4.0f};
	assert(buffer.write(first) == first.size());

	std::array<float, 2> head{};
	assert(buffer.read(head) == head.size());
	assert((head == std::array{1.0f, 2.0f}));

	const std::array second{5.0f, 6.0f, 7.0f, 8.0f, 9.0f};
	assert(buffer.write(second) == 4);
	assert(buffer.size() == 6);

	std::array<float, 6> wrapped{};
	assert(buffer.read(wrapped) == wrapped.size());
	assert((wrapped == std::array{3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f}));
	assert(buffer.size() == 0);

	assert(buffer.write(first) == first.size());
	buffer.clear();
	assert(buffer.size() == 0);
	return 0;
}
