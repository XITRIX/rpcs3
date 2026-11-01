#pragma once

#include "AOTCompiler.h"
#include "AOTRuntime.h"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

class spu_thread;

namespace rpcs3::ios::aot
{
	struct execution_statistics
	{
		std::uint64_t compiled_ppu = 0, compiled_spu = 0;
		std::uint64_t executed_ppu = 0, executed_spu = 0;
		std::uint64_t fallback_ppu = 0, fallback_spu = 0;
		std::uint64_t pending = 0;
		// Optional diagnostic counters; word totals are requested verification
		// work (including failed checks), not guest coverage or a time share.
		std::uint64_t invocations_ppu = 0, invocations_spu = 0;
		std::uint64_t code_checks = 0, code_words_checked = 0;
		std::uint64_t retained_dispatch_tables = 0;
		std::uint64_t native_spu_invocations = 0;
	};

	// Called only while emulation is stopped. Loaded code has process lifetime.
	// Old lookup metadata is reclaimed at this quiescent lifecycle boundary.
	bool known_instruction(std::uint32_t kind, std::uint32_t opcode);
	bool terminates_unit(std::uint32_t kind, std::uint32_t opcode);
	void select_entries(std::span<const rpcs3_aot_entry_v2> entries, std::span<const std::uint32_t> origins = {});
	bool execute(std::uint32_t guest_kind, std::uint32_t opcode, rpcs3_aot_runtime_context_v2& context);
	inline bool execute(const instruction& current, rpcs3_aot_runtime_context_v2& context)
	{
		return execute(current.guest_kind, current.opcode, context);
	}
	struct native_spu_selection
	{
		rpcs3_aot_entry_v2 identity; // Code identity only; invoke is unused.
		void* target;
	};
	bool select_native_spu_entries(std::span<const native_spu_selection> entries);
	bool execute_native_spu(spu_thread& cpu, void* local_store);

	// Native PPU registration installs only prebuilt bridges at these known PCs.
	// The bridge still verifies the selected code before invoking signed entries.
	bool has_prepared_address(std::uint32_t guest_kind, std::uint32_t address);
	std::vector<std::uint32_t> prepared_addresses(std::uint32_t guest_kind, std::uint32_t start, std::uint32_t size);
	execution_statistics statistics() noexcept;
	std::vector<instruction> discoveries();
	void restore_discoveries(std::span<const instruction> entries);
} // namespace rpcs3::ios::aot
