#include "../RPCS3IOSSharedMemory.h"

#include <cassert>
#include <cerrno>
#include <cstdint>
#include <string>

#include <dirent.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int main()
{
	errno = 0;
	assert(rpcs3::ios::create_shared_memory_file({}, 0x10000) == -1);
	assert(errno == EINVAL);

	char directory_template[] = "/tmp/rpcs3-ios-shm-test-XXXXXX";
	const char* directory = ::mkdtemp(directory_template);
	assert(directory);
	const std::string cache_directory = std::string{directory} + "/";

	constexpr std::uint64_t size = 0x10000;
	const int file = rpcs3::ios::create_shared_memory_file(cache_directory, size);
	assert(file >= 0);

	struct stat status{};
	assert(::fstat(file, &status) == 0);
	assert(status.st_size == size);

	void* first = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, file, 0);
	void* second = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, file, 0);
	assert(first != MAP_FAILED);
	assert(second != MAP_FAILED);

	static_cast<std::uint8_t*>(first)[0x1234] = 0xa5;
	assert(static_cast<const std::uint8_t*>(second)[0x1234] == 0xa5);

	assert(::munmap(first, size) == 0);
	assert(::munmap(second, size) == 0);
	assert(::close(file) == 0);

	DIR* contents = ::opendir(directory);
	assert(contents);
	unsigned entries = 0;
	while (const dirent* entry = ::readdir(contents))
	{
		const std::string name = entry->d_name;
		entries += name != "." && name != "..";
	}
	assert(::closedir(contents) == 0);
	assert(entries == 0);
	assert(::rmdir(directory) == 0);
	return 0;
}
