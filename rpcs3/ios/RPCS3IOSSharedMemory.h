#pragma once

#include <cerrno>
#include <cstdint>
#include <limits>
#include <string>

#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

namespace rpcs3::ios
{
inline int create_shared_memory_file(const std::string& cache_directory, std::uint64_t size) noexcept
{
	if (cache_directory.empty() || cache_directory.front() != '/' || cache_directory.back() != '/' ||
		size > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()))
	{
		errno = EINVAL;
		return -1;
	}

	// iOS rejects POSIX shm_open() inside an application sandbox. Use a
	// cache-backed file supplied by the host application, unlinking it as soon
	// as it is open so the descriptor alone owns its lifetime.
	std::string path = cache_directory + "rpcs3-shm-XXXXXX";
	const int file = ::mkstemp(path.data());
	if (file < 0)
	{
		return -1;
	}

	if (::unlink(path.c_str()) != 0 || ::ftruncate(file, static_cast<off_t>(size)) != 0)
	{
		const int error = errno;
		::close(file);
		errno = error;
		return -1;
	}

	return file;
}
}
