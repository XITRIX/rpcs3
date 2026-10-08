// Standalone wait-engine fixture: only formatting/error/main-thread plumbing
// is supplied here; all atomics, parking and notification execute atomic.cpp.
#include "Utilities/StrFmt.h"
#include <cstdlib>
#include <pthread.h>
namespace fmt
{
[[noreturn]] void raw_verify_error(std::source_location, const char8_t*, usz)
{
	std::abort();
}
[[noreturn]] void raw_throw_exception(std::source_location, const char*, const fmt_type_info*, const u64*)
{
	std::abort();
}
}
template <> void fmt_class_string<u32>::format(std::string& out, u64 value)
{
	out += std::to_string(value);
}
template <> void fmt_class_string<u64>::format(std::string& out, u64 value)
{
	out += std::to_string(value);
}
template <> void fmt_class_string<unsigned long>::format(std::string& out, u64 value)
{
	out += std::to_string(value);
}
namespace utils
{
u64 _get_main_tid()
{
	u64 tid = 0;
	pthread_threadid_np(nullptr, &tid);
	return tid;
}
}
