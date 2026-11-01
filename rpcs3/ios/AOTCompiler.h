#pragma once

#include <cstdint>
#include <functional>
#include <compare>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace llvm
{
	class Module;
	class Function;
	class GlobalVariable;
} // namespace llvm

namespace rpcs3::ios::aot
{
	enum class object_platform
	{
		ios,
		host_macos,
		ios_simulator
	};
	struct object_options
	{
		object_platform platform = object_platform::ios;
		std::string cpu = "generic";
		std::string features;
		bool native_spu = false;       // Native GHC program backend; legacy tests remain explicit.
		bool descriptors_only = false; // Explicit resource decline; native interpreter remains authoritative.
	};

	struct instruction
	{
		std::uint32_t guest_kind;
		std::uint32_t opcode;
		std::vector<std::uint32_t> words;
		std::uint32_t address = 0;                                   // native analysis origin (guest addresses, never host pointers)
		std::uint32_t entry_offset = 0;                              // words before the program entry
		std::vector<std::pair<std::uint32_t, std::uint32_t>> blocks; // native PPU blocks or SPU verified nonzero ranges (word offsets/sizes)
		bool operator==(const instruction&) const = default;
		auto operator<=>(const instruction&) const = default;
	};
	struct object_statistics
	{
		std::uint32_t ppu = 0, spu = 0;
		std::map<std::string, std::uint32_t> declined;
	};

	bool supported(const instruction& value);

	struct runtime_import_layout
	{
		llvm::GlobalVariable* table;
		llvm::GlobalVariable* names;
		std::size_t count;
	};
	// Both file ABIs use the same C helper thunks and runtime table representation.
	// Import selection/eligibility belongs to the caller; definitions are materialized
	// here only after their native signatures have been established.
	runtime_import_layout materialize_runtime_imports(llvm::Module& module,
		std::span<llvm::Function* const> imports);

	struct native_entry_definition
	{
		std::uint32_t kind, address, identity_index;
		llvm::Function* target;
	};
	struct native_return_definition
	{
		llvm::GlobalVariable *base, *count, *targets, *chunks;
		std::uint32_t chunk_count;
	};
	llvm::GlobalVariable* emit_native_descriptor(llvm::Module&, std::span<const native_entry_definition>,
		const runtime_import_layout&, std::span<const native_return_definition> = {});

	// File-only LLVM code generation. Does not instantiate an ExecutionEngine,
	// memory manager or JIT allocator. The output is an unrelocated Mach-O object.
	bool validate_ir(const llvm::Module& module, std::string& error, bool (*native_import)(std::string_view) = nullptr);
	// Install target ABI/layout before native lowering infers load/store alignment.
	bool configure_object_module(llvm::Module& module, const object_options& options, std::string& error);
	bool optimize_ppu_object(llvm::Module& module, const object_options& options, std::string& error);
	bool emit_object(llvm::Module& module, const object_options& options,
		std::vector<char>& output, std::string& error);
	// Native GHC output uses the same target/emission backend, with its prebuilt
	// gateway ABI. All imports must already be bound through runtime data slots.
	bool validate_native_ir(const llvm::Module& module, std::string& error);
	bool emit_native_object(llvm::Module& module, const object_options& options,
		std::vector<char>& output, std::string& error);

	// The existing native LLVM translators own all instruction lowering. A null
	// descriptor invokes native interpreter fallback for an unsupported unit.
	bool emit_instructions(std::span<const instruction> entries, const object_options& options,
		std::vector<char>& output, std::string& error, object_statistics* statistics = nullptr,
		const std::function<void(std::size_t)>& checkpoint = {});

	// Static LLD library operation, with no SDK, child process or external symbol
	// resolution. Outputs an ad-hoc placeholder ready for wrapper-owned signing.
	// Explicit exports replace the legacy default; native-only images need no
	// fragment descriptor or fragment registration symbol.
	bool link_module(std::span<const std::string> objects, const std::string& output_path,
		const std::string& install_name, object_platform platform, std::string& error,
		std::span<const std::string> exports = {});
} // namespace rpcs3::ios::aot
