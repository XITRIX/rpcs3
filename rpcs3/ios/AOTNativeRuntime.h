#pragma once

#include <cstdint>
#include <bitset>
#include <span>
#include <vector>
#include "AOTNativeModule.h"

namespace rpcs3::ios::aot
{
	// The native PPU lowering uses RPCS3's ordinary target/segment-table layout.
	// Keep its GHC pointers separate from the C interpreter table. Only registered
	// guest code pages consume backing; none of this storage is executable.
	// Registration, binding and invalidation require stopped CPUs.
	class native_ppu_table
	{
		std::uint8_t* m_storage = nullptr;
		std::bitset<65536> m_pages;

	public:
		native_ppu_table() = default;
		~native_ppu_table();
		native_ppu_table(const native_ppu_table&) = delete;
		native_ppu_table& operator=(const native_ppu_table&) = delete;
		bool register_range(std::uint32_t address, std::uint32_t size);
		bool bind(std::uint32_t address, std::uint32_t segment, void* target);
		bool invalidate(std::uint32_t address, std::uint32_t size);
		void reset();
		std::uint8_t* base() const
		{
			return m_storage;
		}
		void* target(std::uint32_t address) const;
	};

	// Internal loader view, not an on-disk/public ABI. The loader must validate
	// these ranges against an authenticated, retained image before registration.
	struct native_return_binding
	{
		std::uint32_t* base;
		std::uint32_t* count;
		void*** targets;
		std::span<void* const> chunks;
	};

	// Owned for the process lifetime, alongside the loaded native images. Mutations
	// require the core lifecycle lock and stopped CPUs. Tokens are never recycled.
	// Reallocation updates every registered image before execution can resume.
	class native_return_registry
	{
		std::vector<void*> m_targets{nullptr};
		std::vector<native_return_binding> m_bindings;

	public:
		native_return_registry() = default;
		native_return_registry(const native_return_registry&) = delete;
		native_return_registry& operator=(const native_return_registry&) = delete;

		// Reject invalid/duplicate bindings before changing any published data.
		// Allocation failure also leaves the previous generation usable.
		bool append(std::span<const native_return_binding> programs);
		std::uint32_t size() const
		{
			return static_cast<std::uint32_t>(m_targets.size());
		}
	};
	// Retain this object with its dlopen handle until process exit. Admission runs
	// under the lifecycle lock with CPUs stopped; all allocations precede binding.
	// The caller must verify signed bytes and manifest/core/settings/code identities
	// before admission; pointer/ABI validation does not authenticate those identities.
	class native_loaded_module
	{
		const native_module_descriptor* m_descriptor = nullptr;
		std::vector<void*> m_imports;

	public:
		native_loaded_module() = default;
		native_loaded_module(const native_loaded_module&) = delete;
		native_loaded_module& operator=(const native_loaded_module&) = delete;
		bool admit(const native_module_descriptor*, std::span<const native_image_range>, native_return_registry&, std::string& error);
		const native_module_descriptor* descriptor() const
		{
			return m_descriptor;
		}
	};

} // namespace rpcs3::ios::aot
