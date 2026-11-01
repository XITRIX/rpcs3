#include "Emu/Cell/lv2/sys_sync.h"
#include "AOTRecompiler.h"
#include "AOTRuntime.h"
#include "AOTDispatch.h"
#include "AOTNativeModule.h"
#include "Emu/Cell/PPUThread.h"
#include "Emu/system_config.h"
#include "Emu/Cell/PPUTranslator.h"
#include "Emu/Cell/PPUAnalyser.h"

#include <charconv>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>

extern const std::unordered_map<std::string, u64>& ppu_llvm_runtime_symbols();
extern const ppu_decoder<ppu_itype> g_ppu_itype;

namespace rpcs3::ios::aot
{
	u64 resolve_spu_runtime_symbol(std::string_view name);

	object_options native_object_options(std::string_view cpu)
	{
		object_options options;
#ifdef RPCS3_IOS_AOT_SIMULATOR
		options.platform = object_platform::ios_simulator;
#endif
		options.cpu = jit_compiler::cpu(cpu.empty() ? g_cfg.core.llvm_cpu.to_string() : std::string(cpu));
		for (const auto& feature : jit_compiler::target_features())
		{
			if (!options.features.empty())
				options.features += ",";
			options.features += feature;
		}
		return options;
	}

	bool translate_ppu_native(llvm::Module& module, const ppu_module<lv2_obj>& info, std::string_view cpu,
		std::vector<std::pair<std::uint32_t, llvm::Function*>>& entries, const std::function<bool()>& cancelled, bool verify_code)
	{
		entries.clear();
		if (info.segs.empty())
			return false;
		PPUTranslator translator(module.getContext(), &module, info, nullptr, std::string(cpu), PPUTranslator::execution_abi::native);
		if (verify_code)
			translator.set_entry_guard([&](llvm::Function& function, const ppu_function& guest)
				{
					using namespace llvm;
					IRBuilder<> ir(BasicBlock::Create(module.getContext(), "aot_code_check", &function, &function.getEntryBlock()));
					auto* body = &*std::next(function.begin());
					const u32 origin = info.is_relocatable ? info.segs.front().addr : 0;
					auto* range_type = StructType::get(module.getContext(), {ir.getInt32Ty(), ir.getInt32Ty(), ir.getPtrTy()});
					std::vector<Constant*> ranges;
					for (const auto& [address, size] : guest)
					{
						if (!size)
							continue;
						std::vector<u32> words;
						for (u32 offset = 0; offset < size; offset += 4)
							words.push_back(*ensure(info.get_ptr<u32>(address + offset)));
						auto* values = ConstantDataArray::get(module.getContext(), words);
						auto* data = new GlobalVariable(module, values->getType(), true, GlobalValue::PrivateLinkage, values, "ppu_code");
						ranges.push_back(ConstantStruct::get(range_type, {ir.getInt32(address - origin), ir.getInt32(words.size()), data}));
					}
					auto* range_array = ArrayType::get(range_type, ranges.size());
					auto* expected = new GlobalVariable(module, range_array, true, GlobalValue::PrivateLinkage,
						ConstantArray::get(range_array, ranges), "ppu_code_ranges");
					auto* segment = info.is_relocatable ? function.getArg(2) : static_cast<Value*>(ir.getInt64(0));
					auto* rejected = BasicBlock::Create(module.getContext(), "aot_changed_code", &function);
					auto* slow = BasicBlock::Create(module.getContext(), "aot_guard_miss", &function);
					auto* lookup = BasicBlock::Create(module.getContext(), "aot_guard_lookup", &function);
					auto* mapped = BasicBlock::Create(module.getContext(), "aot_guard_mapped", &function);
					auto* bytes = BasicBlock::Create(module.getContext(), "aot_guard_bytes", &function);
					auto* accepted = BasicBlock::Create(module.getContext(), "aot_guard_accepted", &function);
					const auto field = [&](Value* pointer, std::size_t offset, Type* type)
					{
						return ir.CreateLoad(type, ir.CreateConstGEP1_64(ir.getInt8Ty(), pointer, offset));
					};
					const auto acquire = [&](Type* type, Value* pointer)
					{
						auto* load = ir.CreateLoad(type, pointer);
						load->setAtomic(AtomicOrdering::Acquire);
						return load;
					};
					auto* context = field(function.getArg(1), offset32(&ppu_thread::aot_native_context), ir.getPtrTy());
					ir.CreateCondBr(ir.CreateIsNotNull(context), lookup, slow);
					ir.SetInsertPoint(lookup);
					auto* epoch_pointer = field(context, offsetof(native_ppu_guard_context, dispatch_epoch), ir.getPtrTy());
					auto* epoch = acquire(ir.getInt64Ty(), epoch_pointer);
					// Per-entry writable data cannot collide with another entry's proof.
				    // One atomic key covers both the dispatch epoch and guest relocation.
					auto* proof = new GlobalVariable(module, ir.getInt64Ty(), false, GlobalValue::PrivateLinkage,
						ir.getInt64(0), "ppu_handler_proof");
					proof->setAlignment(Align(8));
					auto* key = ir.CreateOr(ir.CreateShl(epoch, 32), segment);
					auto* saved = acquire(ir.getInt64Ty(), proof);
					Value* cached = ir.CreateAnd(ir.CreateICmpNE(saved, ir.getInt64(0)), ir.CreateICmpEQ(saved, key));
					cached = ir.CreateAnd(cached, ir.CreateICmpULE(ir.CreateOr(epoch, segment), ir.getInt64(UINT32_MAX)));
					// The exact entry/segment proof was published only after all range
				    // bounds and alignment were validated. Recheck live mappings/bytes,
				    // not immutable arithmetic already established by this same proof.
					ir.CreateCondBr(cached, mapped, slow);
					ir.SetInsertPoint(mapped);
					auto* pages = field(context, offsetof(native_ppu_guard_context, pages), ir.getPtrTy());
					constexpr u8 flags = vm::page_allocated | vm::page_executable;
					static_assert(sizeof(vm::memory_page) == 1);
					for (const auto& [address, size] : guest)
					{
						if (!size)
							continue;
						const u32 relative = address - origin;
						const u32 page_count = ((relative & 4095) + size + 4095) / 4096;
						auto* first_page = ir.CreateLShr(ir.CreateAdd(segment, ir.getInt64(relative)), 12);
						if (page_count == 1)
						{
							auto* page_flags = acquire(ir.getInt8Ty(), ir.CreateGEP(ir.getInt8Ty(), pages, first_page));
							auto* next = BasicBlock::Create(module.getContext(), "aot_guard_page_ready", &function);
							ir.CreateCondBr(ir.CreateICmpEQ(ir.CreateAnd(page_flags, ir.getInt8(flags)), ir.getInt8(flags)), next, rejected);
							ir.SetInsertPoint(next);
							continue;
						}
						auto* start = ir.GetInsertBlock();
						auto* loop = BasicBlock::Create(module.getContext(), "aot_guard_pages", &function);
						auto* advance = BasicBlock::Create(module.getContext(), "aot_guard_page_next", &function);
						auto* tail = BasicBlock::Create(module.getContext(), "aot_guard_pages_ready", &function);
						ir.CreateBr(loop);
						ir.SetInsertPoint(loop);
						auto* page = ir.CreatePHI(ir.getInt64Ty(), 2);
						page->addIncoming(ir.getInt64(0), start);
						auto* page_flags = acquire(ir.getInt8Ty(), ir.CreateGEP(ir.getInt8Ty(), pages, ir.CreateAdd(first_page, page)));
						ir.CreateCondBr(ir.CreateICmpEQ(ir.CreateAnd(page_flags, ir.getInt8(flags)), ir.getInt8(flags)), advance, rejected);
						ir.SetInsertPoint(advance);
						auto* next = ir.CreateAdd(page, ir.getInt64(1));
						page->addIncoming(next, advance);
						ir.CreateCondBr(ir.CreateICmpULT(next, ir.getInt64(page_count)), loop, tail);
						ir.SetInsertPoint(tail);
					}
					ir.CreateBr(bytes);
					ir.SetInsertPoint(bytes);
					for (const auto& [address, size] : guest)
					{
						auto* pointer = ir.CreateGEP(ir.getInt8Ty(), function.getArg(3), ir.CreateAdd(segment, ir.getInt64(address - origin)));
						u32 offset = 0;
						if (size > 64)
						{
							// GHC shares the gateway's fixed scratch area. An unrolled
						    // comparison of a large guest range can spill its loaded words
						    // over the saved host frame. Keep IR and live values bounded.
							std::vector<u32> words;
							for (u32 i = 0; i < size; i += 4)
								words.push_back(__builtin_bswap32(*ensure(info.get_ptr<u32>(address + i))));
							auto* values = ConstantDataArray::get(module.getContext(), words);
							auto* data = new GlobalVariable(module, values->getType(), true, GlobalValue::PrivateLinkage, values, "ppu_guard_bytes");
							auto* vector_type = FixedVectorType::get(ir.getInt32Ty(), 4);
							auto* start = ir.GetInsertBlock();
							auto* loop = BasicBlock::Create(module.getContext(), "aot_guard_loop", &function);
							auto* advance = BasicBlock::Create(module.getContext(), "aot_guard_advance", &function);
							auto* tail = BasicBlock::Create(module.getContext(), "aot_guard_tail", &function);
							ir.CreateBr(loop);
							ir.SetInsertPoint(loop);
							auto* index = ir.CreatePHI(ir.getInt64Ty(), 2);
							index->addIncoming(ir.getInt64(0), start);
							auto* current = ir.CreateLoad(vector_type, ir.CreateGEP(ir.getInt8Ty(), pointer, index));
							auto* expected_bytes = ir.CreateLoad(vector_type, ir.CreateGEP(ir.getInt8Ty(), data, index));
							current->setAlignment(Align(4));
							expected_bytes->setAlignment(Align(4));
							ir.CreateCondBr(ir.CreateAndReduce(ir.CreateICmpEQ(current, expected_bytes)), advance, rejected);
							ir.SetInsertPoint(advance);
							auto* next = ir.CreateAdd(index, ir.getInt64(16));
							index->addIncoming(next, advance);
							offset = size & ~15u;
							ir.CreateCondBr(ir.CreateICmpULT(next, ir.getInt64(offset)), loop, tail);
							ir.SetInsertPoint(tail);
						}
						for (; offset < size;)
						{
							const u32 width = size - offset >= 16 ? 4 : size - offset >= 8 ? 2 :
						                                                                     1;
							std::vector<u32> expected_words;
							for (u32 i = 0; i < width; ++i)
								expected_words.push_back(__builtin_bswap32(*ensure(info.get_ptr<u32>(address + offset + i * 4))));
							Constant* value = width == 1 ? static_cast<Constant*>(ir.getInt32(expected_words[0])) :
						                      width == 2 ? static_cast<Constant*>(ir.getInt64(u64(expected_words[0]) | (u64(expected_words[1]) << 32))) :
						                                   ConstantDataVector::get(module.getContext(), expected_words);
							auto* current = ir.CreateLoad(value->getType(), ir.CreateConstGEP1_64(ir.getInt8Ty(), pointer, offset));
							current->setAlignment(Align(4));
							auto* same = ir.CreateICmpEQ(current, value);
							auto* next = BasicBlock::Create(module.getContext(), "aot_guard_next", &function);
							ir.CreateCondBr(width < 4 ? same : ir.CreateAndReduce(same), next, rejected);
							ir.SetInsertPoint(next);
							offset += width * 4;
						}
					}
					// A concurrently installed hook cannot turn an old eligibility proof
				    // into permission to skip it. Guest bytes are never cached.
					ir.CreateCondBr(ir.CreateICmpEQ(epoch, acquire(ir.getInt64Ty(), epoch_pointer)), accepted, rejected);
					ir.SetInsertPoint(accepted);
					auto* counter = field(context, offsetof(native_ppu_guard_context, verified_entries), ir.getPtrTy());
					ir.CreateAtomicRMW(AtomicRMWInst::Add, counter, ir.getInt64(1), MaybeAlign(8), AtomicOrdering::Monotonic);
					ir.CreateBr(body);
					ir.SetInsertPoint(slow);
					const auto verify = module.getOrInsertFunction("__aot_ppu_verify", FunctionType::get(ir.getInt1Ty(),
																						   {ir.getPtrTy(), ir.getInt64Ty(), ir.getPtrTy(), ir.getInt32Ty(), ir.getPtrTy()}, false));
					auto* matched = ir.CreateCall(verify, {function.getArg(1), segment, expected, ir.getInt32(ranges.size()), proof});
					ir.CreateCondBr(matched, body, rejected);
					ir.SetInsertPoint(rejected);
					auto* pc = ir.CreateAdd(segment, ir.getInt64(guest.addr - origin));
					ir.CreateStore(ir.CreateTrunc(pc, ir.getInt32Ty()), ir.CreateConstGEP1_64(ir.getInt8Ty(), function.getArg(1), offset32(&ppu_thread::cia)));
					auto fallback = module.getOrInsertFunction("__aot_ppu_fallback", function.getFunctionType());
					cast<Function>(fallback.getCallee())->setCallingConv(CallingConv::GHC);
					std::vector<Value*> arguments;
					for (auto& argument : function.args())
						arguments.push_back(&argument);
					auto* exit = ir.CreateCall(fallback, arguments);
					exit->setCallingConv(CallingConv::GHC);
					exit->setTailCall();
					ir.CreateRetVoid();
				});
		if (!translator.TranslateModule(cancelled))
			return false;
		const auto base = info.is_relocatable ? info.segs.front().addr : 0;
		for (const auto& function : info.get_funcs())
		{
			if (!function.size || std::ranges::find(info.excluded_funcs, function.addr) != info.excluded_funcs.end())
				continue;
			auto native = module.getFunction(fmt::format("__0x%x", function.addr - base));
			if (!native || native->isDeclaration())
				return false;
			entries.emplace_back(function.addr - base, native);
		}
		// External native function references use the existing indirect dispatch
		// lowering, replacing only JIT's generated executable cement stubs.
		std::vector<std::pair<llvm::Function*, u32>> external;
		for (auto& function : module)
		{
			if (!function.isDeclaration() || !function.getName().starts_with("__0x"))
				continue;
			const auto name = function.getName();
			u32 address = 0;
			const auto parsed = std::from_chars(name.data() + 4, name.data() + name.size(), address, 16);
			if (parsed.ec != std::errc{} || parsed.ptr != name.data() + name.size())
				return false;
			external.emplace_back(&function, address);
		}
		for (const auto& [function, address] : external)
			translator.BuildIndirectEntry(*function, address);
		return !entries.empty();
	}

	bool supported(const instruction& value)
	{
		if (!known_instruction(value.guest_kind, value.opcode) || value.address % 4 || value.words.size() > 65536 ||
			value.entry_offset >= std::max<usz>(1, value.words.size()) ||
			(!value.words.empty() && value.words[value.entry_offset] != value.opcode))
			return false;
		if (value.guest_kind == 2 && (value.address >= 0x40000 || value.words.size() > (0x40000 - value.address) / 4))
			return false;
		std::uint64_t end = 0;
		for (const auto& [offset, size] : value.blocks)
		{
			if (!size || offset < end || offset >= value.words.size() || size > value.words.size() - offset)
				return false;
			end = u64(offset) + size;
		}
		return true;
	}

	std::uint64_t resolve_runtime_symbol(std::string_view name)
	{
		// A slow interpreter re-entry is not translated guest code. Units needing
		// these JIT-specific boundaries remain explicit native fallback.
		if (name == "__error" || name == "__resinterp" || name == "__escape")
			return 0;
		const auto& ppu = ppu_llvm_runtime_symbols();
		if (const auto found = ppu.find(std::string(name)); found != ppu.end())
			return found->second;
		return resolve_spu_runtime_symbol(name);
	}

	llvm::Function* translate_ppu(llvm::Module& module, instruction unit, std::string_view name, std::string_view cpu, std::vector<std::uint32_t>& entry_points)
	{
		using namespace llvm;
		auto& context = module.getContext();
		IRBuilder<> ir(context);
		const auto words = unit.words.empty() ? std::span<const u32>(&unit.opcode, 1) : std::span<const u32>(unit.words);
		// Native get_ptr() permits reads through the aligned segment tail. Own that
		// padding explicitly; analysis/translation never sees a guest mapping.
		std::vector<be_t<u32>> storage(utils::align<usz>(words.size() * 4, 0x10000) / 4);
		for (usz i = 0; i < words.size(); ++i)
			storage[i] = words[i];
		ppu_module<lv2_obj> info;
		info.is_relocatable = true;
		info.attr += ppu_attr::has_mfvscr;
		const u32 origin = unit.address ? unit.address : 0x10000;
		info.segs.push_back({origin, ::narrow<u32>(words.size() * 4), 1, 5, ::narrow<u32>(words.size() * 4), storage.data()});
		info.addr_to_seg_index.emplace(origin, 0);
		const auto ptr = ir.getPtrTy();
		const auto type = FunctionType::get(ir.getVoidTy(), {ptr, ptr, ir.getInt64Ty(), ptr, ir.getInt64Ty(), ir.getInt64Ty(), ir.getInt64Ty(), ptr}, false);
		if (unit.blocks.empty())
			unit.blocks.emplace_back(0, words.size());
		// Preserve native function identity while exposing returns from file-only
		// helper/call exits as entries in the same compiled function.
		std::vector<std::pair<u32, u32>> resumable;
		for (const auto& [offset, size] : unit.blocks)
		{
			u32 first = offset;
			for (u32 i = offset; i + 1 < offset + size; ++i)
			{
				const auto kind = g_ppu_itype.decode(words[i]);
				if (kind == ppu_itype::SC || kind == ppu_itype::ISYNC || kind & ppu_itype::branch)
				{
					resumable.emplace_back(first, i + 1 - first);
					first = i + 1;
				}
			}
			resumable.emplace_back(first, offset + size - first);
		}
		unit.blocks = std::move(resumable);
		for (const auto& [offset, size] : unit.blocks)
		{
			const auto block = Function::Create(type, GlobalValue::InternalLinkage, "__0x" + fmt::format("%x", offset * 4), module);
			block->setUWTableKind(UWTableKind::Default);
			block->addParamAttr(7, Attribute::NoAlias);
		}
		PPUTranslator translator(context, &module, info, cpu);
		for (const auto& [offset, size] : unit.blocks)
		{
			ppu_function function;
			function.addr = origin + offset * 4;
			function.size = size * 4;
			if (!translator.Translate(function))
				return nullptr;
		}
		const auto body = module.getFunction("__0x0");
		if (!body || body->empty())
			return nullptr;
		const auto wrapper = Function::Create(FunctionType::get(ir.getVoidTy(), {ptr}, false),
			GlobalValue::InternalLinkage, StringRef(name.data(), name.size()), module);
		wrapper->setUWTableKind(UWTableKind::Default);
		ir.SetInsertPoint(BasicBlock::Create(context, "entry", wrapper));
		const auto runtime = wrapper->getArg(0);
		const auto load = [&](usz offset, Type* type)
		{
			return ir.CreateLoad(type, ir.CreateConstGEP1_64(ir.getInt8Ty(), runtime, offset));
		};
		const auto thread = load(offsetof(rpcs3_aot_runtime_context_v2, cpu), ptr);
		const auto registers = load(offsetof(rpcs3_aot_runtime_context_v2, ppu_gpr), ptr);
		const auto memory = load(offsetof(rpcs3_aot_runtime_context_v2, memory_base), ptr);
		const auto pc = load(offsetof(rpcs3_aot_runtime_context_v2, guest_pc), ir.getInt32Ty());
		const auto base = load(offsetof(rpcs3_aot_runtime_context_v2, code_base_pc), ir.getInt32Ty());
		const auto gpr = [&](u32 index)
		{
			return ir.CreateLoad(ir.getInt64Ty(), ir.CreateConstGEP1_64(ir.getInt64Ty(), registers, index));
		};
		const auto done = BasicBlock::Create(context, "done", wrapper);
		const auto dispatch = ir.CreateSwitch(ir.CreateLShr(ir.CreateSub(pc, base), 2), done, unit.blocks.size());
		for (const auto& [offset, size] : unit.blocks)
		{
			entry_points.push_back(offset);
			const auto entry = BasicBlock::Create(context, "block", wrapper);
			dispatch->addCase(ir.getInt32(offset), entry);
			ir.SetInsertPoint(entry);
			const auto call = ir.CreateCall(module.getFunction(fmt::format("__0x%x", offset * 4)),
				{ConstantPointerNull::get(ptr), thread, ir.CreateZExt(base, ir.getInt64Ty()), memory, gpr(0), gpr(1), gpr(2), runtime});
			(void)call;
			ir.CreateBr(done);
		}
		ir.SetInsertPoint(done);
		const auto next = ir.CreateLoad(ir.getInt32Ty(), ir.CreateConstGEP1_64(ir.getInt8Ty(), thread, ::offset32(&ppu_thread::cia)));
		ir.CreateStore(next, ir.CreateConstGEP1_64(ir.getInt8Ty(), runtime, offsetof(rpcs3_aot_runtime_context_v2, next_pc)));
		ir.CreateRetVoid();
		return wrapper;
	}
} // namespace rpcs3::ios::aot
