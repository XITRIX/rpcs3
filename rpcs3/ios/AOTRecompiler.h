#pragma once

#include "AOTCompiler.h"
#include "util/to_endian.hpp"

template <typename Type>
struct ppu_module;
struct lv2_obj;
struct spu_program;

namespace llvm
{
	class LLVMContext;
	class Function;
	class Value;
} // namespace llvm

namespace rpcs3::ios::aot
{
	// Native file compilation consumes the loader/analyzer's complete metadata.
	// It never reconstructs a native module from instruction-fragment records.
	bool translate_ppu_native(llvm::Module&, const ppu_module<lv2_obj>&, std::string_view cpu,
		std::vector<std::pair<std::uint32_t, llvm::Function*>>& entries,
		const std::function<bool()>& cancelled = {}, bool verify_code = false);
	// Compile one native loader-owned partition into an immutable object. Entry
	// addresses retain the translator's relative/absolute module convention.
	bool emit_ppu_native(const ppu_module<lv2_obj>&, const object_options&,
		std::vector<char>& output, std::string& error,
		const std::function<bool()>& cancelled = {});
	llvm::Function* translate_spu_native(llvm::Module&, const be_t<u32>* local_store, u32 entry,
		std::string_view cpu, std::string_view name, spu_program* identity = nullptr);

	// Bridges the existing translators to file emission. Instruction semantics
	// belong exclusively to PPUTranslator / spu_llvm_recompiler.
	llvm::Function* translate_ppu(llvm::Module&, instruction, std::string_view name, std::string_view cpu, std::vector<std::uint32_t>& entry_points);
	llvm::Function* translate_spu(llvm::Module&, instruction, std::string_view name, std::string_view cpu, std::vector<std::uint32_t>& entry_points);
	std::uint64_t resolve_runtime_symbol(std::string_view name);
	object_options native_object_options(std::string_view cpu = {});

} // namespace rpcs3::ios::aot
