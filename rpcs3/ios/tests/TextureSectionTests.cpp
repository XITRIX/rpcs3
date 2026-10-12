#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include "Utilities/address_range.h"
#include "Emu/RSX/Common/texture_cache_protection_policy.h"
#include "ios/IOSTextureCache.h"
#include "ios/IOSTextureHash.h"
#include "ios/RPCS3IOSExperimentalPolicy.h"
#include "util/fnv_hash.hpp"
#define XXH_INLINE_ALL
#include "Utilities/xxhash3.h"

namespace fmt
{
[[noreturn]] void raw_verify_error(std::source_location, const char8_t*, usz) { std::abort(); }
[[noreturn]] void raw_throw_exception(std::source_location, const char*, const fmt_type_info*, const u64*) { std::abort(); }
}
namespace utils { long get_page_size() { return sysconf(_SC_PAGESIZE); } }
namespace vm { u8* g_sudo_addr; }
static bool tracking;
static unsigned protections;
static utils::protection last_protection;
namespace rpcs3::ios
{
texture_cache_metrics g_texture_validation_metrics;
bool texture_write_tracking_enabled() noexcept { return tracking; }
const experimental_policy& get_experimental_policy() noexcept
{
	static const experimental_policy policy{.texture_hash_hybrid = true};
	return policy;
}
std::uint64_t texture_hash_clock() noexcept { return 1; }
void end_texture_hash(std::uint64_t, std::uint32_t address, std::uint32_t length, bool validation, bool changed) noexcept
{
	g_texture_validation_metrics.record_hash(address, length, 10, validation, changed);
}
}
namespace rsx
{
using utils::address_range32;
using utils::address_range_vector32;
using utils::page_start;
using utils::page_end;
using utils::next_page;
enum section_bounds {full_range, locked_range, confirmed_range};
void memory_protect(const address_range32& range, utils::protection mode)
{
	++protections;
	last_protection = mode;
	const int flags = mode == utils::protection::rw ? PROT_READ | PROT_WRITE :
		mode == utils::protection::ro ? PROT_READ : PROT_NONE;
	assert(mprotect(vm::g_sudo_addr + range.start, range.length(), flags) == 0);
}
}
#include "section.inc"

int main()
{
	using m = rpcs3::ios::texture_cache_metrics;
	auto& metrics = rpcs3::ios::texture_validation_metrics();
	const u32 page = static_cast<u32>(utils::get_page_size());
	const auto allocation = page * 8;
	vm::g_sudo_addr = static_cast<u8*>(mmap(nullptr, allocation, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
	assert(vm::g_sudo_addr != MAP_FAILED);
	std::memset(vm::g_sudo_addr, 0x55, allocation);
	metrics.reset(true);
	rsx::buffered_section section;
	section.reset(utils::address_range32::start_length(page, page * 2));
	section.protect(utils::protection::ro);
	assert(protections == 0 && section.is_locked() && !section.is_locked(true));
	assert(section.sync());
	vm::g_sudo_addr[page * 2 + 123] ^= 1;
	assert(!section.sync());
	assert(metrics.read().totals[m::baseline_calls] == 1);
	assert(metrics.read().totals[m::validation_calls] == 2);
	assert(metrics.read().totals[m::mismatches] == 1);
	section.unprotect();
	assert(section.sync());
	// Confirmed-range hashing must detect changes within the range and avoid
	// hashing unrelated bytes; expanding the range captures a new baseline.
	section.protect(utils::protection::ro, {64, 128});
	vm::g_sudo_addr[page + 1024] ^= 1;
	assert(section.sync());
	vm::g_sudo_addr[page + 127] ^= 1;
	assert(!section.sync());
	section.protect(utils::protection::ro, {32, 4096});
	assert(section.sync());
	section.unprotect();
	section.discard();
	tracking = true;
	metrics.reset(true);
	section.reset(utils::address_range32::start_length(page + 64, 4096));
	section.protect(utils::protection::ro);
	assert(section.is_locked(true) && last_protection == utils::protection::ro);
	assert(section.get_locked_range().start == page);
	assert(section.get_locked_range().end == utils::page_end(page + 64 + 4095));
	for (int i = 0; i < 1000; ++i) assert(section.sync());
	assert(metrics.read().totals[m::locked_checks] == 1000);
	assert(metrics.read().totals[m::validation_calls] == 0);
	assert(metrics.read().totals[m::baseline_calls] == 0);
	// Reuse/unprotect allows the next writer and a fresh protection lifetime.
	section.unprotect();
	assert(last_protection == utils::protection::rw);
	vm::g_sudo_addr[page + 64] = 0x77;
	section.discard();
	section.reset(utils::address_range32::start_length(page, 128));
	const auto calls_before = protections;
	section.protect(utils::protection::ro);
	assert(!section.is_locked(true) && protections == calls_before);
	section.unprotect();
	// GPU ownership remains actual PROT_NONE even for hash-sized sections.
	section.protect(utils::protection::no);
	assert(section.is_locked(true) && last_protection == utils::protection::no);
	section.unprotect();
	assert(last_protection == utils::protection::rw);
	section.discard();
	assert(munmap(vm::g_sudo_addr, allocation) == 0);
	std::puts("Production buffered_section: full/confirmed hash mutation detection, legacy protection, 1000 scan-free validations, page rounding, reuse and GPU no-access passed");
}
