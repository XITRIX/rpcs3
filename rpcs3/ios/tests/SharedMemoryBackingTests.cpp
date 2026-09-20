#include "../RPCS3IOSSharedMemory.h"

#include <cassert>
#include <cerrno>
#include <cstdint>
#include <csignal>
#include <string>

#include <dirent.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void require_write_fault(void* address)
{
	const pid_t child = ::fork();
	assert(child >= 0);
	if (child == 0)
	{
		// Keep the intentional fault inside the child and avoid a crash report.
		const auto fault = [](int) { ::_exit(91); };
		::signal(SIGSEGV, fault);
		::signal(SIGBUS, fault);
		*static_cast<volatile std::uint8_t*>(address) = 0xa5;
		::_exit(0);
	}

	int status = 0;
	assert(::waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 91);
}

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

	auto* reservation = static_cast<std::uint8_t*>(rpcs3::ios::reserve_shared_memory_address_space(nullptr, size * 2));
	assert(reservation != MAP_FAILED);
	require_write_fault(reservation + 0x1234);
	require_write_fault(reservation + size + 0x1234);

	void* first = ::mmap(reservation, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, file, 0);
	assert(first == reservation);
	static_cast<std::uint8_t*>(first)[0x1234] = 0xa5;
	// Mapping one alias must not make the other reserved address writable.
	require_write_fault(reservation + size + 0x1234);
	void* second = ::mmap(reservation + size, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, file, 0);
	assert(second == reservation + size);

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
