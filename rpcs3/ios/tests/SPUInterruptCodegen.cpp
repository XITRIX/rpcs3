#include "Emu/CPU/Backends/AArch64/SPUInterrupts.h"
#include "SPUInterruptFixtures.h"

#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

int main()
{
	llvm::LLVMContext context;
	llvm::Module module("spu_interrupt_tests", context);
	llvm::IRBuilder<> ir(context);
	const auto pointer = llvm::PointerType::getUnqual(context);
	const auto type = llvm::FunctionType::get(ir.getInt32Ty(), {pointer, ir.getInt32Ty()}, false);
	for (unsigned variant = 0; variant < 4; variant++)
	{
		const bool optimized = variant & 1;
		const bool benchmark = variant & 2;
		const auto native = module.getOrInsertFunction(benchmark ? "spu_bench_check_interrupts" : "spu_test_check_interrupts", type);
		const auto name = std::string(benchmark ? "spu_bench_" : "spu_interrupt_") + (optimized ? "fast" : "native");
		const auto function = llvm::Function::Create(type, llvm::GlobalValue::ExternalLinkage,
			name, module);
		ir.SetInsertPoint(llvm::BasicBlock::Create(context, "entry", function));
		const auto slow_path = [&] { return ir.CreateCall(native, {function->getArg(0), function->getArg(1)}); };
		llvm::Value* result;
		if (optimized)
		{
			const auto events = ir.CreateGEP(ir.getInt8Ty(), function->getArg(0), ir.getInt64(offsetof(interrupt_state, events)));
			const auto enabled = ir.CreateGEP(ir.getInt8Ty(), function->getArg(0), ir.getInt64(offsetof(interrupt_state, enabled)));
			result = aarch64::spu_check_interrupts(ir, events, enabled, function->getArg(1), interrupt_busy_mask, slow_path);
			unsigned atomic_loads = 0;
			for (const auto& block : *function)
			{
				for (const auto& instruction : block)
				{
					if (const auto load = llvm::dyn_cast<llvm::LoadInst>(&instruction))
					{
						if (load->getOrdering() != llvm::AtomicOrdering::SequentiallyConsistent) return 1;
						atomic_loads++;
					}
				}
			}
			if (atomic_loads != 2) return 1;
		}
		else result = slow_path();
		ir.CreateRet(result);
	}
	if (llvm::verifyModule(module, &llvm::errs())) return 1;
	module.print(llvm::outs(), nullptr);
}
