#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <array>
#include <atomic>

namespace rpcs3::ios::aot
{
	// Native GHC file ABI. Separate from the retired fragment-context ABI.
	// The containing artifact manifest binds code identities/settings/core/signer;
	// entry indices refer to that manifest, never to process-local addresses.
	inline constexpr std::uint32_t native_module_version = 4;
	// Immutable guest-byte ranges checked before entering a native PPU function.
	// Coordinates use the same module-relative/absolute convention as its entry.
	struct native_ppu_code_range
	{
		std::uint32_t address, words;
		const std::uint32_t* expected;
	};
	static_assert(sizeof(native_ppu_code_range) == 16);
	// Each emitted entry owns one writable atomic eligibility proof. Packing the
	// full guest relocation and a bounded dispatch epoch avoids torn shared state.
	// Once the epoch exceeds 32 bits the proof is disabled, never truncated/reused.
	inline std::uint64_t native_ppu_guard_key(std::uint64_t epoch, std::uint64_t segment)
	{
		return epoch && !(epoch & 1) && epoch <= UINT32_MAX && segment <= UINT32_MAX && !(segment & 8191) ? (epoch << 32) | segment : 0;
	}
	// Guest bytes and mapping permissions are never cached.
	struct native_ppu_guard_context
	{
		const void* pages = nullptr;
		const std::atomic<std::uint64_t>* dispatch_epoch = nullptr;
		std::atomic<std::uint64_t>* verified_entries = nullptr;
		std::atomic<std::uint64_t>* verification_misses = nullptr;
	};
	const void* native_ppu_page_registry();
	struct native_module_entry
	{
		std::uint32_t kind;    // 1 PPU, 2 SPU
		std::uint32_t address; // Native PPU coordinate (manifest owns relocation) or SPU LS entry
		std::uint32_t identity_index;
		std::uint32_t reserved;
		void* target;
	};
	struct native_module_returns
	{
		std::uint32_t* base;
		std::uint32_t* count;
		void*** targets;
		void* const* chunks;
		std::uint32_t chunk_count;
		std::uint32_t reserved;
	};
	struct native_module_descriptor
	{
		std::uint32_t size;
		std::uint32_t version;
		std::uint32_t entry_count;
		std::uint32_t import_count;
		const native_module_entry* entries;
		const char* const* imports;
		void*** import_table;
		std::uint32_t return_count;
		std::uint32_t reserved;
		const native_module_returns* returns;
	};
	static_assert(sizeof(native_module_entry) == 24 && sizeof(native_module_returns) == 40 && sizeof(native_module_descriptor) == 56);

	// Mapped-image permissions, supplied by the OS loader adapter. No descriptor
	// pointer is dereferenced until the entire corresponding range is admitted.
	struct native_image_range
	{
		std::uintptr_t address;
		std::size_t size;
		bool readable, writable, executable;
	};
	bool inspect_native_image(const void* symbol, std::vector<native_image_range>& ranges, std::string& error);
	bool validate_native_module(const native_module_descriptor* module,
		std::span<const native_image_range> ranges, std::string& error);
} // namespace rpcs3::ios::aot
