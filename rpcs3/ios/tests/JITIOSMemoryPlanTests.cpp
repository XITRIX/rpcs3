#include "../../../Utilities/JITIOSLayoutPolicy.h"
#include "../../../Utilities/JITIOSMemoryPlan.h"

#include <cassert>

int main()
{
	using rpcs3::ios::jit::code_allocation;
	using rpcs3::ios::jit::code_allocation_plan;
	using namespace rpcs3::ios::jit;

	constexpr usz mib = 1024 * 1024;
	constexpr usz commit_granularity = 512 * 1024;
	code_allocation_plan plan(16 * mib, commit_granularity);
	code_allocation allocation;

	assert(plan.allocate(384 * 1024, 16, allocation));
	assert(allocation.offset == 0);
	assert(allocation.committed_offset == 0);
	assert(allocation.committed_size == commit_granularity);

	// This section would cross the first alias boundary without padding. It must
	// instead begin in a new mapping so writable(offset, size) stays contiguous.
	assert(plan.allocate(256 * 1024, 16, allocation));
	assert(allocation.offset == commit_granularity);
	assert(allocation.committed_offset == commit_granularity);
	assert(allocation.committed_size == commit_granularity);

	assert(plan.allocate(64 * 1024, 64, allocation));
	assert(allocation.offset == 768 * 1024);
	assert(!allocation.needs_commit());

	// A section larger than one granule receives one larger contiguous mapping.
	assert(plan.allocate(1280 * 1024, 16, allocation));
	assert(allocation.offset == mib);
	assert(allocation.committed_offset == mib);
	assert(allocation.committed_size == 1536 * 1024);

	assert(!plan.allocate(14 * mib, 16, allocation));
	assert(!plan.allocate(1, 3, allocation));
	assert(!plan.allocate(0, 16, allocation));

	static_assert(llvm_region_capacity == 32 * mib);
	static_assert(llvm_layout_size == 64 * mib);
	static_assert(ppu_modules_per_jit == 25);

	code_allocation_plan bounded_plan(llvm_region_capacity, commit_granularity);
	assert(bounded_plan.allocate(llvm_region_capacity, 16, allocation));
	assert(allocation.committed_size == llvm_region_capacity);
	assert(!bounded_plan.allocate(1, 16, allocation));
	return 0;
}
