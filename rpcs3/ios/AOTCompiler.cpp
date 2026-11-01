#include "AOTCompiler.h"
#include "AOTRuntime.h"
#include "AOTNativeModule.h"
#include "AOTNativeImports.h"
#include "Emu/Cell/SPURecompiler.h"
#include "AOTRecompiler.h"
#include <llvm/Linker/Linker.h>

#include <algorithm>
#include <memory>

#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>

namespace rpcs3::ios::aot
{
	namespace
	{
		llvm::Value* field(llvm::IRBuilder<>& builder, llvm::Value* base, std::size_t offset, llvm::Type* type)
		{
			auto* address = builder.CreateConstGEP1_64(builder.getInt8Ty(), base, offset);
			return builder.CreateLoad(type, address);
		}
	} // namespace
	runtime_import_layout materialize_runtime_imports(llvm::Module& module,
		std::span<llvm::Function* const> imports)
	{
		auto& context = module.getContext();
		llvm::IRBuilder<> b(context);
		auto* pointer = b.getPtrTy();
		auto* runtime_table = new llvm::GlobalVariable(module, pointer, false, llvm::GlobalValue::PrivateLinkage,
			llvm::ConstantPointerNull::get(pointer), "runtime_imports");
		std::vector<llvm::Constant*> import_names;
		for (std::size_t i = 0; i < imports.size(); ++i)
		{
			auto* function = imports[i];
			b.SetInsertPoint(llvm::BasicBlock::Create(context, "runtime_call", function));
			auto* table = b.CreateLoad(pointer, runtime_table);
			auto* address = b.CreateLoad(pointer, b.CreateConstGEP1_64(pointer, table, i));
			std::vector<llvm::Value*> arguments;
			for (auto& argument : function->args())
				arguments.push_back(&argument);
			auto* call = b.CreateCall(function->getFunctionType(), address, arguments);
			call->setCallingConv(function->getCallingConv());
			if (function->getCallingConv() == llvm::CallingConv::GHC)
				call->setTailCall();
			if (function->getReturnType()->isVoidTy())
				b.CreateRetVoid();
			else
				b.CreateRet(call);
			function->setLinkage(llvm::GlobalValue::InternalLinkage);
			function->setUWTableKind(llvm::UWTableKind::Default);
			function->setMemoryEffects(llvm::MemoryEffects::unknown());
			import_names.push_back(b.CreateGlobalString(function->getName()));
		}
		auto* names_type = llvm::ArrayType::get(pointer, import_names.size());
		auto* names = new llvm::GlobalVariable(module, names_type, true, llvm::GlobalValue::PrivateLinkage,
			llvm::ConstantArray::get(names_type, import_names), "import_names");
		return {runtime_table, names, import_names.size()};
	}

	llvm::GlobalVariable* emit_native_descriptor(llvm::Module& module, std::span<const native_entry_definition> entries,
		const runtime_import_layout& imports, std::span<const native_return_definition> returns)
	{
		using namespace llvm;
		auto& context = module.getContext();
		IRBuilder<> b(context);
		auto* pointer = b.getPtrTy();
		auto* word = b.getInt32Ty();
		auto* entry_type = StructType::get(context, {word, word, word, word, pointer});
		std::vector<Constant*> values;
		for (const auto& entry : entries)
			values.push_back(ConstantStruct::get(entry_type, {b.getInt32(entry.kind), b.getInt32(entry.address),
																 b.getInt32(entry.identity_index), b.getInt32(0), entry.target}));
		auto* entry_array = ArrayType::get(entry_type, values.size());
		auto* entry_table = new GlobalVariable(module, entry_array, true, GlobalValue::PrivateLinkage,
			ConstantArray::get(entry_array, values), "native_entries");
		auto* return_type = StructType::get(context, {pointer, pointer, pointer, pointer, word, word});
		values.clear();
		for (const auto& item : returns)
			values.push_back(ConstantStruct::get(return_type, {item.base, item.count, item.targets, item.chunks,
																  b.getInt32(item.chunk_count), b.getInt32(0)}));
		auto* return_array = ArrayType::get(return_type, values.size());
		auto* return_table = new GlobalVariable(module, return_array, true, GlobalValue::PrivateLinkage,
			ConstantArray::get(return_array, values), "native_returns");
		auto* descriptor_type = StructType::get(context, {word, word, word, word, pointer, pointer, pointer, word, word, pointer});
		return new GlobalVariable(module, descriptor_type, true, GlobalValue::ExternalLinkage,
			ConstantStruct::get(descriptor_type, {b.getInt32(sizeof(native_module_descriptor)), b.getInt32(native_module_version),
													 b.getInt32(entries.size()), b.getInt32(imports.count), entry_table, imports.names, imports.table,
													 b.getInt32(returns.size()), b.getInt32(0), return_table}),
			"rpcs3_aot_native_module");
	}

	bool emit_ppu_native(const ppu_module<lv2_obj>& info, const object_options& options,
		std::vector<char>& output, std::string& error, const std::function<bool()>& cancelled)
	{
		output.clear();
		error.clear();
		llvm::LLVMContext context;
		llvm::Module module("rpcs3_native_ppu", context);
		if (!configure_object_module(module, options, error))
			return false;
		std::vector<std::pair<std::uint32_t, llvm::Function*>> functions;
		if (!translate_ppu_native(module, info, options.cpu, functions, cancelled, true))
		{
			error = "Native PPU translation declined or cancelled";
			return false;
		}
		std::vector<llvm::Function*> imports;
		for (auto it = module.begin(); it != module.end();)
		{
			auto& function = *it++;
			if (!function.isDeclaration() || function.isIntrinsic())
				continue;
			if (function.use_empty())
			{
				function.eraseFromParent();
				continue;
			}
			if (!resolve_native_import(function.getName().str()))
			{
				error = "Unavailable native PPU import: " + function.getName().str();
				return false;
			}
			imports.push_back(&function);
		}
		const auto imported = materialize_runtime_imports(module, imports);
		std::vector<native_entry_definition> entries;
		for (const auto& [address, function] : functions)
			entries.push_back({1, address, static_cast<std::uint32_t>(entries.size()), function});
		emit_native_descriptor(module, entries, imported);
		// TranslateModule already runs the normal PPU lowering/frame passes.
		// Do not optimize native GHC functions a second time as fragment wrappers.
		if (cancelled && cancelled())
		{
			error = "Native PPU compilation cancelled";
			return false;
		}
		return emit_native_object(module, options, output, error);
	}

	bool emit_instructions(std::span<const instruction> entries, const object_options& options, std::vector<char>& output, std::string& error, object_statistics* statistics, const std::function<void(std::size_t)>& checkpoint)
	{
		output.clear();
		error.clear();
		if (entries.empty() || entries.size() > 4096)
		{
			error = "Invalid native AOT batch size";
			return false;
		}
		if (statistics)
			*statistics = {};
		llvm::LLVMContext context;
		llvm::Module module("rpcs3_native_aot_v2", context);
		// Keep LLVM's native type/metadata mapping for the complete batch. The
		// static linkModules convenience call reconstructs IRMover and rescans the
		// growing destination for every function/program.
		llvm::Linker linker(module);
		if (!configure_object_module(module, options, error))
			return false;
		llvm::IRBuilder<> b(context);
		auto* pointer = b.getPtrTy();
		auto* entry_type = llvm::StructType::get(context, {b.getInt32Ty(), b.getInt32Ty(), b.getInt32Ty(), b.getInt32Ty(), pointer, pointer, b.getInt32Ty(), b.getInt32Ty(), pointer});
		std::vector<llvm::Constant*> descriptors;
		std::vector<std::vector<std::uint32_t>> entry_points(entries.size());
		std::vector<std::string> function_names(entries.size());
		std::vector<native_entry_definition> native_entries;
		std::vector<native_return_definition> native_returns;
		std::vector<bool> native_spu_entries(entries.size());
		// One native program may be observed at several resumable entries. Keep
		// every descriptor, but reuse a proven entry of an already translated body.
		std::map<instruction, std::vector<std::size_t>> translated;
		// PPU uses the native optimizer on a bounded module partition, rather
		// than rebuilding the full O2 pipeline for every individual function.
		// SPU already optimizes its native program and is never optimized twice.
		std::unique_ptr<llvm::Module> ppu_partition;
		std::unique_ptr<llvm::Linker> ppu_linker;
		std::size_t ppu_words = 0, ppu_functions = 0;
		const auto flush_ppu = [&]()
		{
			if (!ppu_partition)
				return true;
			ppu_linker.reset();
			if (!optimize_ppu_object(*ppu_partition, options, error))
				return false;
			if (linker.linkInModule(std::move(ppu_partition)))
			{
				error = "Cannot combine optimized native PPU output";
				return false;
			}
			ppu_words = 0;
			ppu_functions = 0;
			return true;
		};
		for (std::size_t i = 0; i < entries.size(); ++i)
		{
			if (checkpoint)
				checkpoint(i);
			const auto& unit = entries[i];
			const std::string name = "entry_" + std::to_string(i);
			function_names[i] = name;
			if (options.native_spu && unit.guest_kind == 2)
			{
				if (options.descriptors_only || !supported(unit))
					continue;
				const auto words = unit.words.empty() ? std::span<const u32>(&unit.opcode, 1) : std::span<const u32>(unit.words);
				const u32 origin = unit.address || !unit.blocks.empty() ? unit.address : 0x100;
				if (origin >= 0x40000 || words.size() > (0x40000 - origin) / 4 || unit.entry_offset >= words.size())
					continue;
				std::vector<be_t<u32>> ls(65536);
				for (usz n = 0; n < words.size(); ++n)
					ls[origin / 4 + n] = words[n];
				auto part = std::make_unique<llvm::Module>(name, context);
				part->setTargetTriple(module.getTargetTriple());
				part->setDataLayout(module.getDataLayout());
				spu_program identity;
				auto* target = translate_spu_native(*part, ls.data(), origin + unit.entry_offset * 4, options.cpu, name, &identity);
				if (!target || identity.lower_bound < origin ||
					u64(identity.lower_bound) + identity.data.size() * 4 > u64(origin) + words.size() * 4)
				{
					if (statistics)
						++statistics->declined["Native SPU analysis needs additional code"];
					continue;
				}
				bool bound = true;
				for (auto it = part->begin(); it != part->end();)
				{
					auto& function = *it++;
					if (!function.isDeclaration() || function.isIntrinsic())
						continue;
					if (function.use_empty())
					{
						function.eraseFromParent();
						continue;
					}
					if (!resolve_native_import(function.getName().str()))
					{
						bound = false;
						break;
					}
				}
				if (!bound)
				{
					if (statistics)
						++statistics->declined["Native SPU helper unavailable"];
					continue;
				}
				const auto target_name = target->getName().str();
				const auto field = [&](const char* suffix, llvm::Type* type)
				{
					const auto symbol = name + ".aot_spu_return_" + suffix;
					if (auto* value = part->getNamedGlobal(symbol))
						return value;
					return new llvm::GlobalVariable(*part, type, false, llvm::GlobalValue::InternalLinkage,
						llvm::Constant::getNullValue(type), symbol);
				};
				auto* chunks = part->getNamedGlobal(name + ".aot_spu_return_chunks");
				if (!chunks)
				{
					error = "Missing native SPU return metadata";
					return false;
				}
				const auto chunk_count = llvm::cast<llvm::ArrayType>(chunks->getValueType())->getNumElements();
				auto* base_field = field("base", b.getInt32Ty());
				auto* count_field = field("count", b.getInt32Ty());
				auto* targets_field = field("targets", pointer);
				// Keep the globals referenced while IRMover transfers the program.
				auto* view_type = llvm::ArrayType::get(pointer, 4);
				new llvm::GlobalVariable(*part, view_type, true, llvm::GlobalValue::ExternalLinkage,
					llvm::ConstantArray::get(view_type, {base_field, count_field, targets_field, chunks}), name + ".bindings");
				if (linker.linkInModule(std::move(part)))
				{
					error = "Cannot combine native SPU program";
					return false;
				}
				native_entries.push_back({2, origin + unit.entry_offset * 4, static_cast<u32>(i), module.getFunction(target_name)});
				native_returns.push_back({module.getNamedGlobal(name + ".aot_spu_return_base"), module.getNamedGlobal(name + ".aot_spu_return_count"),
					module.getNamedGlobal(name + ".aot_spu_return_targets"), module.getNamedGlobal(name + ".aot_spu_return_chunks"), static_cast<u32>(chunk_count)});
				module.getNamedGlobal(name + ".bindings")->setLinkage(llvm::GlobalValue::InternalLinkage);
				native_spu_entries[i] = true;
				if (statistics)
					++statistics->spu;
				continue;
			}
			auto key = unit;
			key.entry_offset = 0;
			key.opcode = unit.words.empty() ? unit.opcode : unit.words.front();
			const bool eligible = !options.descriptors_only && supported(unit);
			if (eligible)
				if (const auto found = translated.find(key); found != translated.end())
					if (const auto existing = std::find_if(found->second.begin(), found->second.end(), [&](std::size_t candidate)
							{
								return std::find(entry_points[candidate].begin(), entry_points[candidate].end(), unit.entry_offset) != entry_points[candidate].end();
							});
						existing != found->second.end())
					{
						function_names[i] = function_names[*existing];
						entry_points[i] = entry_points[*existing];
						if (statistics)
							(unit.guest_kind == 1 ? statistics->ppu : statistics->spu)++;
						continue;
					}
			auto native = std::make_unique<llvm::Module>(name, context);
			native->setTargetTriple(module.getTargetTriple());
			native->setDataLayout(module.getDataLayout());
			auto* function = eligible ? (unit.guest_kind == 1 ? translate_ppu(*native, unit, name, options.cpu, entry_points[i]) : translate_spu(*native, unit, name, options.cpu, entry_points[i])) : nullptr;
			// Native intrinsic replacement may leave unused declarations behind.
			// They are not runtime dependencies and must not disable the unit.
			for (auto it = native->begin(); it != native->end();)
			{
				auto& declaration = *it++;
				if (declaration.isDeclaration() && declaration.use_empty())
					declaration.eraseFromParent();
			}
			std::string unsupported;
			if (function && validate_ir(*native, unsupported, +[](std::string_view name)
								{
									return resolve_runtime_symbol(name) != 0;
								}))
			{
				function->setLinkage(llvm::GlobalValue::ExternalLinkage);
				if (unit.guest_kind == 1)
				{
					const auto words = std::max<std::size_t>(1, unit.words.size());
					if (ppu_functions && (ppu_functions >= 64 || words > 4096 - std::min<std::size_t>(4096, ppu_words)))
						if (!flush_ppu())
							return false;
					if (!ppu_partition)
					{
						ppu_partition = std::make_unique<llvm::Module>("native_ppu_partition", context);
						ppu_partition->setTargetTriple(module.getTargetTriple());
						ppu_partition->setDataLayout(module.getDataLayout());
						ppu_linker = std::make_unique<llvm::Linker>(*ppu_partition);
					}
					if (ppu_linker->linkInModule(std::move(native)))
					{
						error = "Cannot combine native PPU functions";
						return false;
					}
					ppu_words += words;
					++ppu_functions;
				}
				else if (linker.linkInModule(std::move(native)))
				{
					error = "Cannot combine native SPU output";
					return false;
				}
				translated[std::move(key)].push_back(i);
				if (statistics)
					(unit.guest_kind == 1 ? statistics->ppu : statistics->spu)++;
			}
			else
			{
				if (statistics)
					++statistics->declined[options.descriptors_only ? "Native unit exceeds signed module size limit" :
										   unsupported.empty()      ? "Native translator declined code" :
																	  unsupported];
				function = nullptr;
			}
		}
		if (checkpoint)
			checkpoint(entries.size());
		if (!flush_ppu())
			return false;
		for (std::size_t i = 0; i < entries.size(); ++i)
		{
			const auto& unit = entries[i];
			auto* function = native_spu_entries[i] ? nullptr : module.getFunction(function_names[i]);
			if (function)
				function->setLinkage(llvm::GlobalValue::InternalLinkage);
			const auto words = unit.words.empty() ? std::span<const std::uint32_t>(&unit.opcode, 1) : std::span<const std::uint32_t>(unit.words);
			auto* code = new llvm::GlobalVariable(module, llvm::ArrayType::get(b.getInt32Ty(), words.size()), true,
				llvm::GlobalValue::PrivateLinkage, llvm::ConstantDataArray::get(context, llvm::ArrayRef(words.data(), words.size())), "guest_code");
			auto& points_for_entry = entry_points[i];
			std::sort(points_for_entry.begin(), points_for_entry.end());
			points_for_entry.erase(std::unique(points_for_entry.begin(), points_for_entry.end()), points_for_entry.end());
			auto* points = new llvm::GlobalVariable(module, llvm::ArrayType::get(b.getInt32Ty(), points_for_entry.size()), true,
				llvm::GlobalValue::PrivateLinkage, llvm::ConstantDataArray::get(context, llvm::ArrayRef(points_for_entry)), "entry_offsets");
			descriptors.push_back(llvm::ConstantStruct::get(entry_type, {b.getInt32(unit.guest_kind), b.getInt32(unit.opcode),
																			b.getInt32(words.size()), b.getInt32(unit.entry_offset), code, function ? static_cast<llvm::Constant*>(function) : llvm::ConstantPointerNull::get(pointer), b.getInt32(points_for_entry.size()), b.getInt32(unit.guest_kind == 2 && !unit.blocks.empty() ? RPCS3_AOT_SPARSE_SPU : 0), points}));
		}
		if (checkpoint)
			checkpoint(entries.size());
		// Resolve native helper calls through one registered, immutable data table.
		// Its contents belong to the current process; files contain only names.
		std::vector<llvm::Function*> imports;
		for (auto& function : module)
			if (function.isDeclaration() && !function.isIntrinsic())
				imports.push_back(&function);
		const auto imported = materialize_runtime_imports(module, imports);
		if (!native_entries.empty())
			emit_native_descriptor(module, native_entries, imported, native_returns);
		auto* runtime_table = imported.table;
		auto* names = imported.names;
		auto* array_type = llvm::ArrayType::get(entry_type, entries.size());
		auto* array = new llvm::GlobalVariable(module, array_type, true, llvm::GlobalValue::InternalLinkage,
			llvm::ConstantArray::get(array_type, descriptors), "entries");
		auto* descriptor_type = llvm::StructType::get(context, {b.getInt32Ty(), b.getInt32Ty(), b.getInt32Ty(), b.getInt32Ty(), pointer,
																   b.getInt32Ty(), b.getInt32Ty(), pointer});
		auto* descriptor = new llvm::GlobalVariable(module, descriptor_type, true, llvm::GlobalValue::InternalLinkage,
			llvm::ConstantStruct::get(descriptor_type, {b.getInt32(sizeof(rpcs3_aot_module_v2)), b.getInt32(RPCS3_AOT_RUNTIME_VERSION),
														   b.getInt32(entries.size()), b.getInt32(0), array, b.getInt32(imported.count), b.getInt32(0), names}),
			"descriptor");
		auto* getter = llvm::Function::Create(llvm::FunctionType::get(pointer, false), llvm::GlobalValue::ExternalLinkage, "rpcs3_aot_module_v2", module);
		b.SetInsertPoint(llvm::BasicBlock::Create(context, "entry", getter));
		b.CreateRet(descriptor);
		auto* registration = llvm::Function::Create(llvm::FunctionType::get(b.getInt32Ty(), {pointer}, false),
			llvm::GlobalValue::ExternalLinkage, "rpcs3_aot_register_runtime_v2", module);
		b.SetInsertPoint(llvm::BasicBlock::Create(context, "entry", registration));
		auto* runtime = registration->getArg(0);
		auto* size = field(b, runtime, offsetof(rpcs3_aot_runtime_context_v2, structure_size), b.getInt32Ty());
		auto* version = field(b, runtime, offsetof(rpcs3_aot_runtime_context_v2, version), b.getInt32Ty());
		auto* count = field(b, runtime, offsetof(rpcs3_aot_runtime_context_v2, runtime_function_count), b.getInt32Ty());
		auto* table = field(b, runtime, offsetof(rpcs3_aot_runtime_context_v2, runtime_functions), pointer);
		auto* valid = b.CreateAnd(b.CreateICmpUGE(size, b.getInt32(sizeof(rpcs3_aot_runtime_context_v2))), b.CreateICmpEQ(version, b.getInt32(RPCS3_AOT_RUNTIME_VERSION)));
		valid = b.CreateAnd(valid, b.CreateICmpUGE(count, b.getInt32(imported.count)));
		if (!imports.empty())
			valid = b.CreateAnd(valid, b.CreateIsNotNull(table));
		auto* accept = llvm::BasicBlock::Create(context, "accept", registration);
		auto* reject = llvm::BasicBlock::Create(context, "reject", registration);
		b.CreateCondBr(valid, accept, reject);
		b.SetInsertPoint(accept);
		b.CreateStore(table, runtime_table);
		b.CreateRet(b.getInt32(1));
		b.SetInsertPoint(reject);
		b.CreateRet(b.getInt32(0));
		if (checkpoint)
			checkpoint(entries.size());
		return native_entries.empty() ? emit_object(module, options, output, error) : emit_native_object(module, options, output, error);
	}

} // namespace rpcs3::ios::aot
