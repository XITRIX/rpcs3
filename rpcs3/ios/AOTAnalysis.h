#pragma once
#include "AOTCompiler.h"
#include "util/to_endian.hpp"
#include <functional>
#include <map>
#include <span>

template <typename Type>
struct ppu_module;
struct lv2_obj;

namespace rpcs3::ios::aot
{
	// Native ELF loaders and PPU/SPU analyzers, operating on owned file buffers.
	// No guest mappings, CPU threads or executable allocations are created.
	using instruction_inventory = std::map<instruction, std::uint64_t>;
	void collect_ppu_functions(const ppu_module<lv2_obj>& module, instruction_inventory& inventory,
		const std::function<void()>& checkpoint = {});
	void observe_ppu_functions(const ppu_module<lv2_obj>& module);
	// Runtime discovery copies bounded raw LS snapshots only on newly observed
	// misses. Native analysis happens after Stop, without CPU/JIT initialization.
	void observe_spu_program(const void* owner, const be_t<u32>* local_store, std::uint32_t entry,
		std::span<const std::uint32_t> observed);
	void collect_runtime_spu_programs(instruction_inventory& inventory);
	void clear_runtime_spu_programs();
	void collect_spu_program(const be_t<u32>* local_store, std::uint32_t entry, instruction_inventory& inventory);
	void collect_native_block(std::uint32_t kind, const be_t<u32>* words, std::uint32_t count, instruction_inventory& inventory);
	// A native-discovered raw SPU segment has an estimated LS base, but no ELF
	// entry. Exact guest bytes still select code at runtime, independently of base.
	void analyze_spu_segment(std::uint32_t address, std::span<const std::uint8_t> bytes,
		instruction_inventory& inventory, const std::function<void()>& checkpoint = {});
	void collect_cached_spu_programs(const std::string& directory, instruction_inventory& inventory, const std::function<void()>& checkpoint = {});
	// Native loader mode leaves PPU analysis to the real loader; still scan each
	// validated container for embedded SPU programs.
	struct analysis_result
	{
		std::uint32_t ppu_images = 0, spu_images = 0;
	};
	analysis_result analyze_elf(std::span<const std::uint8_t> bytes, instruction_inventory& inventory,
		const std::function<void()>& checkpoint = {}, bool include_ppu = true);
} // namespace rpcs3::ios::aot
