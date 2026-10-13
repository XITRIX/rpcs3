#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using u64 = uint64_t;
using usz = size_t;
#define FORCE_INLINE inline
#define SAFE_BUFFERS(...) __VA_ARGS__
struct
{
	struct
	{
		bool perf_report = false;
	} core;
} g_cfg;
static unsigned reads, reports;
static u64 reported_start;
namespace utils
{
	u64 get_tsc()
	{
		return 100 + ++reads;
	}
} // namespace utils
template <auto Name>
struct perf_stat
{
	static void push(u64 start)
	{
		++reports;
		reported_start = start;
	}
};
#include "PerfMeter.inc"
static void check(bool result)
{
	if (!result)
		std::abort();
}
int main()
{
	for (bool reporting : {false, true})
	{
		g_cfg.core.perf_report = reporting;
		reads = reports = 0;
		{
			perf_meter<1> meter(nullptr);
			check(reads == unsigned(reporting));
			check(meter.get() == (reporting ? 101 : 0));
		}
		check(reports == unsigned(reporting));
		if (reporting)
			check(reported_start == 101);

		// Logical-timing users retain the default constructor even when
		// reporting is disabled (notably GETLLAR and STORE128).
		reads = reports = 0;
		{
			perf_meter<2> meter;
			check(reads == 1 && meter.get() == 101);
		}
		check(reports == unsigned(reporting));

		reads = reports = 0;
		{
			perf_meter<3> parent(nullptr);
			perf_meter<4> child = parent;
			check(reads == unsigned(reporting));
			check(child.get() == parent.get());
		}
		check(reports == 2 * unsigned(reporting));

		reads = reports = 0;
		{
			perf_meter<5> meter(nullptr);
			meter.reset();
		}
		check(reads == unsigned(reporting) && reports == 0);
	}
	std::puts("PASS: production perf_meter disabled/enabled, start preservation, copies and reset; both reservation call sites guarded");
}
