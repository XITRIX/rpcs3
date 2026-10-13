#include "Emu/Cell/SPUARM64FloatConvert.h"
#include <llvm/IR/Verifier.h>
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
	auto* floats = FixedVectorType::get(ir.getFloatTy(), 4);
	auto* integers = FixedVectorType::get(ir.getInt32Ty(), 4);

	for (bool is_signed : {false, true})
	for (bool candidate : {false, true})
	{
		const std::string name = std::string(is_signed ? "signed_" : "unsigned_") + (candidate ? "candidate" : "baseline");
		auto* function = Function::Create(FunctionType::get(ir.getVoidTy(),
			{ir.getPtrTy(), ir.getPtrTy(), ir.getInt32Ty()}, false), Function::ExternalLinkage, name, module);
		auto* entry = BasicBlock::Create(context, "entry", function);
		auto* loop = BasicBlock::Create(context, "loop", function);
		auto* end = BasicBlock::Create(context, "end", function);
		ir.SetInsertPoint(entry);
		ir.CreateBr(loop);
		ir.SetInsertPoint(loop);
		auto* index = ir.CreatePHI(ir.getInt32Ty(), 2);
		index->addIncoming(ir.getInt32(0), entry);
		auto* value = ir.CreateAlignedLoad(floats,
			ir.CreateGEP(ir.getInt32Ty(), function->getArg(0), index), Align(4));
		Value* result;
		if (candidate)
		{
			result = spu_llvm::arm64::convert_scaled_float(ir, value, is_signed);
		}
		else
		{
			// Original approximate production expression. Observe its actual
			// AArch64 output, including native FCVTZ saturation. An independent
			// integer oracle also checks every result; this is not our oracle.
			auto* bits = ir.CreateBitCast(value, integers);
			auto* converted = is_signed ? ir.CreateFPToSI(value, integers) : ir.CreateFPToUI(value, integers);
			if (is_signed)
				converted = ir.CreateSelect(ir.CreateFCmpUNO(value, value), ConstantInt::get(integers, 0x80000000), converted);
			else
				converted = ir.CreateAnd(converted, ir.CreateNot(ir.CreateAShr(bits, ConstantInt::get(integers, 31))));
			result = ir.CreateSelect(ir.CreateICmpSGT(bits, ConstantInt::get(integers, is_signed ? 0x4effffff : 0x4f7fffff)),
				ConstantInt::get(integers, is_signed ? 0x7fffffff : 0xffffffff), converted);
		}
		ir.CreateAlignedStore(result, ir.CreateGEP(ir.getInt32Ty(), function->getArg(1), index), Align(4));
		auto* next = ir.CreateAdd(index, ir.getInt32(4));
		index->addIncoming(next, loop);
		ir.CreateCondBr(ir.CreateICmpULT(next, function->getArg(2)), loop, end);
		ir.SetInsertPoint(end);
		ir.CreateRetVoid();
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
