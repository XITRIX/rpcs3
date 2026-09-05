#include "Emu/CPU/Backends/AArch64/SPUChecksum.h"

#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

int main()
{
	llvm::LLVMContext context;
	llvm::Module module("spu_checksum_tests", context);
	llvm::IRBuilder<> ir(context);
	const auto vector_type = llvm::FixedVectorType::get(ir.getInt32Ty(), 4);
	const auto type = llvm::FunctionType::get(ir.getInt32Ty(), {vector_type, vector_type, vector_type, vector_type}, false);

	for (bool optimized : {false, true})
	{
		const auto function = llvm::Function::Create(type, llvm::GlobalValue::ExternalLinkage,
			optimized ? "spu_checksum_neon" : "spu_checksum_scalar", module);
		ir.SetInsertPoint(llvm::BasicBlock::Create(context, "entry", function));
		std::array<llvm::Value*, 4> differences;
		for (unsigned i = 0; i < differences.size(); i++)
		{
			differences[i] = function->getArg(i);
		}

		llvm::Value* mismatch;
		if (optimized)
		{
			// Exercise the same emitter called by SPULLVMRecompiler.cpp.
			mismatch = aarch64::spu_checksum_mismatch(ir, differences);
		}
		else
		{
			// Previous generated reduction, retained only as a differential and
			// code-generation baseline. The runner also has an independent oracle.
			llvm::Value* scalar = nullptr;
			for (const auto difference : differences)
			{
				const auto lanes = ir.CreateBitCast(difference, llvm::FixedVectorType::get(ir.getInt64Ty(), 2));
				for (unsigned lane = 0; lane < 2; lane++)
				{
					const auto value = ir.CreateExtractElement(lanes, lane);
					scalar = scalar ? ir.CreateOr(scalar, value) : value;
				}
			}
			mismatch = ir.CreateICmpNE(scalar, ir.getInt64(0));
		}
		ir.CreateRet(ir.CreateZExt(mismatch, ir.getInt32Ty()));
	}

	if (llvm::verifyModule(module, &llvm::errs()))
	{
		return 1;
	}
	module.print(llvm::outs(), nullptr);
}
