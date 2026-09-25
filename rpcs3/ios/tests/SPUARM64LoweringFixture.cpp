#include "Emu/Cell/SPUARM64Lowering.h"
#include <llvm/AsmParser/Parser.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Transforms/Scalar/EarlyCSE.h>
#include <llvm/Transforms/Scalar/SimplifyCFG.h>
#include <llvm/Transforms/Scalar/DeadStoreElimination.h>
#include <llvm/Transforms/Scalar/LICM.h>
#include <llvm/Transforms/Scalar/LoopPassManager.h>
#include <llvm/Transforms/Scalar/ADCE.h>
int main(int argc, char** argv)
{
	if (argc != 3 && argc != 4)
		return 2;
	llvm::LLVMContext context;
	llvm::SMDiagnostic diagnostic;
	auto module = llvm::parseAssemblyFile(argv[1], diagnostic, context);
	if (!module)
	{
		diagnostic.print(argv[0], llvm::errs());
		return 1;
	}
	llvm::SmallVector<llvm::Function*, 64> functions;
	for (auto& fn : *module)
		if (fn.getName().ends_with("_baseline"))
			functions.push_back(&fn);
	unsigned total = 0;
	for (auto* original : functions)
	{
		const auto name = original->getName().drop_back(9).str() + "_candidate";
		if (auto* old = module->getFunction(name))
			old->eraseFromParent();
		llvm::ValueToValueMapTy mapping;
		auto* candidate = llvm::CloneFunction(original, mapping);
		candidate->setName(name);
		unsigned changed = 0;
		if (original->getName().starts_with("splat_"))
		{
			llvm::SmallVector<llvm::CallInst*, 4> calls;
			for (auto& block : *candidate)
				for (auto& inst : block)
					if (auto* call = llvm::dyn_cast<llvm::CallInst>(&inst); call && call->getCalledFunction() &&
						call->getCalledFunction()->getIntrinsicID() == llvm::Intrinsic::aarch64_neon_tbx2)
						calls.push_back(call);
			for (auto* call : calls)
			{
				const bool constant_a = llvm::isa<llvm::Constant>(call->getArgOperand(1));
				auto* splat = llvm::cast<llvm::Constant>(call->getArgOperand(constant_a ? 1 : 2))->getSplatValue();
				auto* base = llvm::cast<llvm::CallInst>(call->getArgOperand(0));
				auto* shift = llvm::cast<llvm::BinaryOperator>(base->getArgOperand(1));
				auto* mask = llvm::cast<llvm::BinaryOperator>(call->getArgOperand(3));
				llvm::IRBuilder<> ir(call);
				auto* result = spu_llvm::arm64::shuffle_with_splat(ir, call->getArgOperand(constant_a ? 2 : 1),
					shift->getOperand(0), mask->getOperand(0), llvm::cast<llvm::ConstantInt>(splat)->getZExtValue(), constant_a);
				call->replaceAllUsesWith(result);
				call->eraseFromParent();
				++changed;
			}
		}
		changed += spu_llvm::arm64::fold_byte_reversals(*candidate);
		total += changed;
		llvm::outs() << name << ": " << changed << " folds\n";
		if (original->getName().starts_with("guard_") ? changed != 0 : changed != 1)
		{
			llvm::errs() << "Unexpected transformation count for " << name << '\n';
			return 1;
		}
	}
	if (!total || llvm::verifyModule(*module, &llvm::errs()))
		return 1;
	if (argc == 4)
	{
		// Match the SPU recompiler's post-lowering IR pipeline. llc then
		// performs code generation without adding a generic -O3 pipeline.
		llvm::LoopAnalysisManager lam;
		llvm::FunctionAnalysisManager fam;
		llvm::CGSCCAnalysisManager cgam;
		llvm::ModuleAnalysisManager mam;
		llvm::PassBuilder pb;
		pb.registerModuleAnalyses(mam);
		pb.registerCGSCCAnalyses(cgam);
		pb.registerFunctionAnalyses(fam);
		pb.registerLoopAnalyses(lam);
		pb.crossRegisterProxies(lam, fam, cgam, mam);
		llvm::FunctionPassManager fpm;
		fpm.addPass(llvm::EarlyCSEPass(true));
		fpm.addPass(llvm::SimplifyCFGPass());
		fpm.addPass(llvm::DSEPass());
		fpm.addPass(llvm::createFunctionToLoopPassAdaptor(llvm::LICMPass(llvm::LICMOptions()), true));
		fpm.addPass(llvm::ADCEPass());
		for (auto& fn : *module)
			if (!fn.isDeclaration())
				fpm.run(fn, fam);
		if (llvm::verifyModule(*module, &llvm::errs()))
			return 1;
	}
	std::error_code error;
	llvm::raw_fd_ostream output(argv[2], error, llvm::sys::fs::OF_None);
	if (error)
		return 1;
	module->print(output, nullptr);
}
