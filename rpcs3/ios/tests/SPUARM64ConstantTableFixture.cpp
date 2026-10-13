#include "Emu/Cell/SPUARM64ConstantTable.h"
#include <llvm/Transforms/Utils/Local.h>
#include <llvm/IR/IntrinsicsAArch64.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Target/TargetMachine.h>

using namespace llvm;

int main(int argc, char** argv)
{
	if (argc != 2)
		return 2;

	InitializeNativeTarget();
	InitializeNativeTargetAsmPrinter();
	LLVMContext context;
	Module module("conversion", context);
	IRBuilder<> ir(context);
	Triple triple("arm64-apple-macosx14.0.0");
	std::string error;
	const auto* target = TargetRegistry::lookupTarget(triple, error);
	if (!target)
	{
		errs() << error;
		return 1;
	}
	std::unique_ptr<TargetMachine> machine(target->createTargetMachine(triple, "apple-m1", "",
		TargetOptions{}, Reloc::PIC_, std::nullopt, CodeGenOptLevel::Aggressive));
	module.setTargetTriple(triple);
	module.setDataLayout(machine->createDataLayout());
	auto* bytes = FixedVectorType::get(ir.getInt8Ty(), 16);
	for (unsigned kind = 0; kind < 68; ++kind)
		for (bool chain : {false, true})
			for (bool candidate : {false, true})
			{
				const std::string name = "table_" + std::to_string(kind) + (chain ? "_chain" : "_batch") + (candidate ? "_candidate" : "_baseline");
				auto* function = Function::Create(FunctionType::get(ir.getVoidTy(),
													  {ir.getPtrTy(), ir.getPtrTy(), ir.getInt32Ty()}, false),
					Function::ExternalLinkage, name, module);
				auto* entry = BasicBlock::Create(context, "entry", function);
				auto* loop = BasicBlock::Create(context, "loop", function);
				auto* end = BasicBlock::Create(context, "end", function);
				ir.SetInsertPoint(entry);
				ir.CreateBr(loop);
				ir.SetInsertPoint(loop);
				auto* index = ir.CreatePHI(ir.getInt32Ty(), 2);
				index->addIncoming(ir.getInt32(0), entry);
				auto* state = chain ? ir.CreatePHI(bytes, 2) : nullptr;
				if (state)
					state->addIncoming(Constant::getNullValue(bytes), entry);
				Value* value = ir.CreateAlignedLoad(bytes, ir.CreateGEP(bytes, function->getArg(0), index), Align(1));
				if (state)
					value = ir.CreateXor(value, state);
				SmallVector<Constant*, 16> first, second;
				for (unsigned lane = 0; lane < 16; ++lane)
				{
					unsigned x, y;
					if (kind == 0)
					{
						x = lane;
						y = lane;
					}
					else if (kind == 1)
					{
						x = 15 - lane;
						y = 15 - lane;
					}
					else if (kind == 2)
					{
						x = (lane + 4) & 15;
						y = (lane + 12) & 15;
					}
					else if (kind == 3)
					{
						x = lane - 2;
						y = lane + 3;
					}
					else if (kind < 20)
					{
						x = (lane + kind - 4) % 16;
						y = (lane * 7 + 3) % 16;
					}
					else if (kind < 36)
					{
						x = (lane + kind - 20) % 32;
						y = lane;
					}
					else if (kind < 52)
					{
						x = (lane * 3 + kind) % 256;
						y = (kind - 36) * 17;
					}
					else
					{
						x = (kind * 13 + lane * 7) % 256;
						y = (kind * 29 + lane * 3) % 32;
					}
					first.push_back(ir.getInt8(x));
					second.push_back(ir.getInt8(y));
				}
				auto* lookup = Intrinsic::getOrInsertDeclaration(&module, Intrinsic::aarch64_neon_tbl1, {bytes});
				auto* inner = ir.CreateCall(lookup, {value, ConstantVector::get(first)});
				auto* result = ir.CreateCall(lookup, {inner, ConstantVector::get(second)});
				if (state)
					state->addIncoming(result, loop);
				ir.CreateAlignedStore(result, ir.CreateGEP(bytes, function->getArg(1), index), Align(1));
				auto* next = ir.CreateAdd(index, ir.getInt32(1));
				index->addIncoming(next, loop);
				ir.CreateCondBr(ir.CreateICmpULT(next, function->getArg(2)), loop, end);
				ir.SetInsertPoint(end);
				ir.CreateRetVoid();
				// Baseline includes all existing byte-reversal/table simplifications.
				spu_llvm::arm64::fold_byte_reversals(*function);
				spu_llvm::arm64::fold_single_source_tables(*function);
				if (candidate && spu_llvm::arm64::fold_constant_tables(*function) == 0)
				{
					errs() << "Expected constant-table fold in " << name << '\n';
					return 4;
				}
				bool deleted;
				do
				{
					deleted = false;
					for (auto& block : *function)
						for (auto i = block.begin(); i != block.end();)
						{
							auto* instruction = &*i++;
							if (isInstructionTriviallyDead(instruction))
							{
								instruction->eraseFromParent();
								deleted = true;
							}
						}
				} while (deleted);
			}
	// Guard tests: dynamic, partially undefined and shared table inputs.
	for (unsigned mode = 0; mode < 3; ++mode)
	{
		auto* function = Function::Create(FunctionType::get(bytes, {bytes, bytes}, false),
			Function::ExternalLinkage, "guard_" + std::to_string(mode), module);
		ir.SetInsertPoint(BasicBlock::Create(context, "entry", function));
		auto* lookup = Intrinsic::getOrInsertDeclaration(&module, Intrinsic::aarch64_neon_tbl1, {bytes});
		SmallVector<Constant*, 16> mask;
		for (unsigned i = 0; i < 16; ++i)
			mask.push_back(ir.getInt8((i + 3) % 16));
		Value* indices = function->getArg(1);
		if (mode == 1)
		{
			mask[7] = UndefValue::get(ir.getInt8Ty());
			indices = ConstantVector::get(mask);
		}
		auto* inner = ir.CreateCall(lookup, {function->getArg(0), mode == 2 ? ConstantVector::get(mask) : indices});
		Value* result = inner;
		if (mode == 2)
		{
			auto* outer = ir.CreateCall(lookup, {inner, ConstantVector::get(mask)});
			result = ir.CreateXor(inner, outer); // Inner has two live uses.
		}
		ir.CreateRet(result);
		if (spu_llvm::arm64::fold_constant_tables(*function) != 0)
			return 3;
	}
	if (verifyModule(module, &errs()))
		return 1;
	std::error_code code;
	raw_fd_ostream ir_output(std::string(argv[1]) + ".ll", code);
	if (code)
		return 1;
	module.print(ir_output, nullptr);
	raw_fd_ostream object(std::string(argv[1]) + ".o", code);
	if (code)
		return 1;
	legacy::PassManager passes;
	if (machine->addPassesToEmitFile(passes, object, nullptr, CodeGenFileType::ObjectFile))
		return 1;
	passes.run(module);
}
