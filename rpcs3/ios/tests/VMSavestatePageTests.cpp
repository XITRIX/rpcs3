#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

using u8 = uint8_t;
using u32 = uint32_t;
using u64 = uint64_t;

// Only page metadata is modeled. The generated functions come from vm.cpp.
static std::array<u8, 1024> g_pages{};

namespace fmt
{
template <typename... Args>
[[noreturn]] void throw_exception(const char* message, Args...)
{
	throw std::runtime_error(message);
}
}

#include "VMSavestatePagesUnderTest.inc"

static void check(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

static void populate(u32 pages, u8 body, bool guarded)
{
	g_pages.fill(0);
	std::fill_n(g_pages.begin() + 1, pages, body);
	if (guarded)
	{
		g_pages[1] = page_allocated;
		g_pages[pages] = page_allocated;
	}
}

static void expect_inconsistency(u32 pages)
{
	try
	{
		unmap_size(4096, pages * 4096);
	}
	catch (const std::runtime_error& error)
	{
		check(std::string(error.what()).find("Memory inconsistency") != std::string::npos,
			"wrong rejection reason");
		return;
	}
	throw std::runtime_error("inconsistent pages were accepted");
}

int main()
{
	try
	{
		unsigned cases = 0;
		for (const u64 block_size : {block_size_4k, block_size_64k, block_size_1m})
		{
			for (const u8 access : {u8{0}, u8{page_readable}, u8{page_readable | page_writable}})
			{
				for (const bool executable : {false, true})
				{
					const u8 expected = page_allocated | access | (executable ? page_executable : 0) |
						(block_size == block_size_64k ? page_64k_size : block_size == block_size_1m ? page_1m_size : 0);
					const u64 restored = restore_flags(block_size | preallocated, expected);
					check((restored & block_size_mask) == block_size, "restored allocation lost its page size");
					check(allocation_flags(restored) == expected, "restored access or executable flags changed");
					for (const u32 pages : {1u, 16u, 256u, 512u})
					{
						populate(pages, expected, false);
						check(unmap_size(4096, pages * 4096) == pages * 4096, "restored mapping failed cleanup");
						++cases;
					}
				}
			}
		}

		// Normal stacks, restored stacks, and states re-saved by an older broken
		// loader must all retain 4K interiors and allocated-only guard pages.
		const u64 stack = block_size_4k | preallocated | stack_guarded | bf0_0x1;
		for (const u8 access : {u8{0}, u8{page_readable}, u8{page_readable | page_writable}})
		{
			for (const bool stale_size_bit : {false, true})
			{
				const u8 serialized = page_allocated | access | (stale_size_bit ? page_1m_size : 0);
				const u64 restored = restore_flags(stack, serialized);
				const u8 body = allocation_flags(restored);
				check(body == (page_allocated | access), "restored stack has large-page metadata");
				const u64 fresh = stack | (access & page_readable ? 0 : alloc_hidden) |
					(access & page_writable ? 0 : alloc_unwritable);
				check(body == allocation_flags(fresh), "restored stack differs from fresh stack");
				for (u32 pages = 3; pages <= 32; ++pages)
				{
					populate(pages, body, true);
					check(unmap_size(4096, pages * 4096) == pages * 4096, "stack failed whole-block cleanup");
					check(unmap_size(8192, (pages - 2) * 4096) == (pages - 2) * 4096, "stack failed interior cleanup");
					++cases;
				}
			}
		}

		// Keep the validator strict: both the old bug and genuine executable or
		// page-size mismatches must still be rejected.
		populate(3, page_allocated | page_readable | page_writable | page_1m_size, true);
		expect_inconsistency(3);
		populate(3, page_allocated | page_readable | page_64k_size, false);
		g_pages[2] |= page_executable;
		expect_inconsistency(3);
		g_pages[2] = page_allocated | page_readable | page_1m_size;
		expect_inconsistency(3);
		std::printf("VM save-state page tests passed: %u mapping cases and 3 consistency rejection controls\n", cases);
	}
	catch (const std::exception& error)
	{
		std::fprintf(stderr, "VM save-state page test failed: %s\n", error.what());
		return 1;
	}
}
