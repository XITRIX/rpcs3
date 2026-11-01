#include "AOTCompiler.h"
#include <llvm/ADT/ScopeExit.h>
#include <llvm/Support/Signals.h>
#include "AOTRuntime.h"
#include <llvm/Passes/PassBuilder.h>

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <memory>
#include <mutex>
#include <unordered_set>

#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <lld/Common/Driver.h>

LLD_HAS_DRIVER(macho)

namespace rpcs3::ios::aot
{
	namespace
	{
		std::mutex linker_mutex;
		bool linker_usable = true;

		bool approved_vector_select(const llvm::InlineAsm& assembly)
		{
			// Preserve the native SPU FCGT workaround for LLVM's ARM64 select codegen.
			// This exact instruction operates only on vector registers; it has no
			// process addresses, memory effects, control transfers or JIT ABI boundary.
			const auto* type = assembly.getFunctionType();
			const auto* vector = llvm::dyn_cast<llvm::FixedVectorType>(type->getReturnType());
			return assembly.getAsmString() == "bsl $0.16b, $1.16b, $2.16b" &&
			       assembly.getConstraintString() == "=w,w,w,0" && !assembly.hasSideEffects() &&
			       !assembly.isAlignStack() && !assembly.canThrow() && !type->isVarArg() &&
			       vector && vector->getNumElements() == 4 && vector->getElementType()->isIntegerTy(32) &&
			       type->getNumParams() == 3 && type->getParamType(0) == vector &&
			       type->getParamType(1) == vector && type->getParamType(2) == vector;
		}

		bool approved_native_frame_assembly(const llvm::InlineAsm& assembly)
		{
			// Exact forms emitted by the shared ARM64 GHC preservation pass with
			// gateway-owned scratch and debug frames disabled. Keep arbitrary assembly,
			// reserved-register writes and Universal JIT traps outside the file backend.
			const auto* type = assembly.getFunctionType();
			if (!assembly.hasSideEffects() || assembly.isAlignStack() || assembly.canThrow() ||
				type->isVarArg() || !type->getReturnType()->isVoidTy())
				return false;
			const auto& code = assembly.getAsmString();
			const auto& constraints = assembly.getConstraintString();
			if (code == "mov x30, $0")
				return constraints == "r,~{x30}" &&
				       type->getNumParams() == 1 && type->getParamType(0)->isPointerTy();
			if (type->getNumParams())
				return false;
			if (code == "brk #153")
				return constraints.empty(); // Native unreachable-tail diagnostic.
			const std::string_view prefix = "ldr x30, [x30, #";
			if (!code.starts_with(prefix) || !code.ends_with(']') || constraints != "~{x30}")
				return false;
			const std::string_view digits(code.data() + prefix.size(), code.size() - prefix.size() - 1);
			unsigned offset = 0;
			const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), offset);
			return parsed.ec == std::errc{} && parsed.ptr == digits.data() + digits.size() && offset < 65536 && offset % 8 == 0;
		}

		bool embeds_address(const llvm::Constant* value)
		{
			if (const auto* expression = llvm::dyn_cast<llvm::ConstantExpr>(value);
				expression && expression->getOpcode() == llvm::Instruction::IntToPtr)
				return true;
			// Global values may reference each other cyclically. Their initializers are
			// visited separately, so follow only aggregate/constant-expression operands.
			if (llvm::isa<llvm::GlobalValue>(value))
				return false;
			for (const auto& operand : value->operands())
				if (const auto* constant = llvm::dyn_cast<llvm::Constant>(operand); constant && embeds_address(constant))
					return true;
			return false;
		}

		llvm::TargetMachine* object_target(const object_options& options, std::string& error)
		{
			static std::once_flag initialized;
			std::call_once(initialized, []
				{
					llvm::InitializeNativeTarget();
					llvm::InitializeNativeTargetAsmPrinter();
					llvm::InitializeNativeTargetAsmParser();
				});
			// TargetMachine is native LLVM compiler state, independent of module
			// ownership. Reuse it on this worker across wrapper optimization/codegen;
			// contexts and pass managers remain isolated, and no JIT manager is created.
			struct cached_target
			{
				object_options options;
				std::unique_ptr<llvm::TargetMachine> machine;
			};
			thread_local cached_target cached;
			if (cached.machine && cached.options.platform == options.platform && cached.options.cpu == options.cpu &&
				cached.options.features == options.features)
				return cached.machine.get();
			const std::string triple = options.platform == object_platform::ios           ? "arm64-apple-ios16.0" :
			                           options.platform == object_platform::ios_simulator ? "arm64-apple-ios16.0-simulator" :
			                                                                                "arm64-apple-macosx14.0";
			const auto* target = llvm::TargetRegistry::lookupTarget(llvm::Triple(triple), error);
			if (!target)
				return {};
			auto machine = std::unique_ptr<llvm::TargetMachine>(target->createTargetMachine(
				llvm::Triple(triple), options.cpu, options.features, llvm::TargetOptions{}, llvm::Reloc::PIC_,
				llvm::CodeModel::Small, llvm::CodeGenOptLevel::Aggressive));
			if (!machine)
				return nullptr;
			cached.options = options;
			cached.machine = std::move(machine);
			return cached.machine.get();
		}

	} // namespace

	static bool validate_ir_impl(const llvm::Module& module, std::string& error, bool (*native_import)(std::string_view), bool native_abi)
	{
		error.clear();
		if (!module.getModuleInlineAsm().empty())
		{
			error = "AOT module assembly is unsupported";
			return false;
		}
		for (const auto& global : module.globals())
		{
			if (global.isDeclaration() || (global.hasInitializer() && embeds_address(global.getInitializer())))
			{
				error = "AOT globals must not capture process addresses";
				return false;
			}
		}
		for (const auto& function : module)
		{
			if (function.isIntrinsic())
				continue;
			if (function.isDeclaration() && (!native_import || !native_import(function.getName().str())))
			{
				error = "Unavailable native AOT import: " + function.getName().str();
				return false;
			}
			// The PPU optimizer may specialize private C functions to fastcc. Only
			// accept that after native-input validation; exported boundaries stay C.
			if (function.getCallingConv() != llvm::CallingConv::C &&
				!(native_abi && (function.getCallingConv() == llvm::CallingConv::GHC ||
									(function.hasLocalLinkage() && function.getCallingConv() == llvm::CallingConv::PreserveMost))) &&
				!(native_import == nullptr && function.hasLocalLinkage() && function.getCallingConv() == llvm::CallingConv::Fast))
			{
				error = "Unsupported AOT calling convention in " + function.getName().str();
				return false;
			}
			for (const auto& block : function)
				for (const auto& operation : block)
				{
					if (llvm::isa<llvm::IntToPtrInst>(operation) && (!native_abi || llvm::isa<llvm::Constant>(operation.getOperand(0))))
					{
						error = "AOT instructions must not capture process addresses";
						return false;
					}
					for (const auto& operand : operation.operands())
					{
						if (const auto* assembly = llvm::dyn_cast<llvm::InlineAsm>(operand); assembly && !approved_vector_select(*assembly) &&
																							 !(native_abi && function.getCallingConv() == llvm::CallingConv::GHC && approved_native_frame_assembly(*assembly)))
						{
							error = "AOT inline assembly is unsupported";
							return false;
						}
						if (const auto* constant = llvm::dyn_cast<llvm::Constant>(operand); constant && embeds_address(constant))
						{
							error = "AOT instructions must not capture process addresses";
							return false;
						}
					}
				}
		}
		llvm::raw_string_ostream diagnostics(error);
		return !llvm::verifyModule(module, &diagnostics);
	}

	bool validate_ir(const llvm::Module& module, std::string& error, bool (*native_import)(std::string_view))
	{
		return validate_ir_impl(module, error, native_import, false);
	}

	bool validate_native_ir(const llvm::Module& module, std::string& error)
	{
		return validate_ir_impl(module, error, nullptr, true);
	}

	bool configure_object_module(llvm::Module& module, const object_options& options, std::string& error)
	{
		auto machine = object_target(options, error);
		if (!machine)
		{
			if (error.empty())
				error = "Cannot create the ARM64 AOT target";
			return false;
		}
		module.setTargetTriple(machine->getTargetTriple());
		module.setDataLayout(machine->createDataLayout());
		return true;
	}

	bool optimize_ppu_object(llvm::Module& module, const object_options& options, std::string& error)
	{
		// PPU's file wrappers need IR optimization. SPU has already run the shared
		// native CFG/SSA optimization pipeline; do not run a second O2 pipeline on it.
		auto machine = object_target(options, error);
		if (!machine)
			return false;
		llvm::PassBuilder builder(machine);
		llvm::LoopAnalysisManager loops;
		llvm::FunctionAnalysisManager functions;
		llvm::CGSCCAnalysisManager graph;
		llvm::ModuleAnalysisManager modules;
		builder.registerLoopAnalyses(loops);
		builder.registerFunctionAnalyses(functions);
		builder.registerCGSCCAnalyses(graph);
		builder.registerModuleAnalyses(modules);
		builder.crossRegisterProxies(loops, functions, graph, modules);
		auto optimize = builder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
		optimize.run(module, modules);
		return true;
	}

	static bool emit_object_impl(llvm::Module& module, const object_options& options, std::vector<char>& output, std::string& error, bool native_abi)
	{
		output.clear();
		error.clear();
		if (!validate_ir_impl(module, error, nullptr, native_abi))
			return false;
		// Darwin reserves x18. Native GHC output has already passed the shared
		// preservation pass; fragment output retains its original C frame policy.
		auto machine = object_target(options, error);
		if (!machine)
		{
			error = "Cannot create the ARM64 AOT target";
			return false;
		}
		module.setTargetTriple(machine->getTargetTriple());
		module.setDataLayout(machine->createDataLayout());
		for (auto& function : module)
			if (!native_abi && !function.isDeclaration())
			{
				// Native helpers can unwind an interrupt/stop. Use normal Apple
				// frame records and asynchronous unwind metadata, including thunks.
				function.addFnAttr("frame-pointer", "all");
				function.setUWTableKind(llvm::UWTableKind::Async);
			}
		llvm::SmallVector<char, 0> bytes;
		llvm::raw_svector_ostream stream(bytes);
		llvm::legacy::PassManager passes;
		if (machine->addPassesToEmitFile(passes, stream, nullptr, llvm::CodeGenFileType::ObjectFile, false))
		{
			error = "ARM64 Mach-O object emission is unavailable";
			return false;
		}
		passes.run(module);
		output.assign(bytes.begin(), bytes.end());
		return !output.empty();
	}

	bool emit_object(llvm::Module& module, const object_options& options, std::vector<char>& output, std::string& error)
	{
		return emit_object_impl(module, options, output, error, false);
	}

	bool emit_native_object(llvm::Module& module, const object_options& options, std::vector<char>& output, std::string& error)
	{
		return emit_object_impl(module, options, output, error, true);
	}

	bool link_module(std::span<const std::string> objects, const std::string& output_path, const std::string& install_name,
		object_platform platform, std::string& error, std::span<const std::string> exports)
	{
		std::lock_guard lock(linker_mutex);
		error.clear();
		if (!linker_usable)
		{
			error = "The static linker cannot be reused after an internal failure";
			return false;
		}
		if (objects.empty() || objects.size() > 128 || !std::filesystem::path(output_path).is_absolute() ||
			std::filesystem::exists(output_path) || !install_name.starts_with("@rpath/rpcs3-aot-") ||
			install_name.find('\0') != std::string::npos || install_name.find("..") != std::string::npos)
		{
			error = "Invalid AOT linker output or install identity";
			return false;
		}
		for (const auto& path : objects)
			if (!std::filesystem::path(path).is_absolute() || !std::filesystem::is_regular_file(path) || path.find('\0') != std::string::npos)
			{
				error = "Invalid AOT linker object path";
				return false;
			}
		std::vector<std::string> arguments = {"ld64.lld", "-dylib", "-arch", "arm64", "-platform_version",
			platform == object_platform::ios ? "ios" : platform == object_platform::ios_simulator ? "ios-simulator" :
																									"macos",
			platform == object_platform::host_macos ? "14.0" : "16.0", platform == object_platform::host_macos ? "14.0" : "16.0", "-o", output_path, "-install_name", install_name,
			"-undefined", "error", "-adhoc_codesign", "-headerpad", "0x4000"};
		const std::string legacy_exports[] = {"_rpcs3_aot_module_v2", "_rpcs3_aot_register_runtime_v2"};
		if (exports.empty())
			exports = legacy_exports;
		for (const auto& symbol : exports)
		{
			if (symbol != "_rpcs3_aot_native_module" && symbol != "_rpcs3_aot_module_v2" && symbol != "_rpcs3_aot_register_runtime_v2")
			{
				error = "Unsupported AOT export";
				return false;
			}
			arguments.push_back("-exported_symbol");
			arguments.push_back(symbol);
		}
		arguments.insert(arguments.end(), objects.begin(), objects.end());
		std::vector<const char*> argv;
		for (const auto& argument : arguments)
			argv.push_back(argument.c_str());
		std::string messages;
		llvm::raw_string_ostream out(messages), diagnostics(error);
		const lld::DriverDef drivers[] = {{lld::Darwin, &lld::macho::link}};
		// LLD's temporary output files install LLVM's process-wide fatal-signal
		// handlers. Guest memory protection faults belong to RPCS3 after linking.
		// Reset LLVM's registry as well as restoring its saved native handlers, so
		// repeated links can install/retire their own handlers independently.
		const auto restore_handlers = llvm::make_scope_exit([]
			{
				llvm::sys::unregisterHandlers();
			});
		const auto result = lld::lldMain(argv, out, diagnostics, drivers);
		linker_usable = result.canRunAgain;
		if (result.retCode != 0 || !linker_usable)
		{
			std::error_code ignored;
			std::filesystem::remove(output_path, ignored);
			if (error.empty())
				error = "AOT Mach-O link failed";
			return false;
		}
		return true;
	}
} // namespace rpcs3::ios::aot
