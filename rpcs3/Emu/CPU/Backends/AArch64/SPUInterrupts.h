#pragma once

#include <cstdint>
#include <llvm/IR/IRBuilder.h>

namespace aarch64
{
	// Inline only the ordinary enable-with-no-pending-event case. Unsupported
	// masks and pending interrupts retain the complete native helper.
	template <typename SlowPath>
	llvm::Value* spu_check_interrupts(llvm::IRBuilder<>& ir, llvm::Value* events,
		llvm::Value* enabled, llvm::Value* address, std::uint32_t busy_mask, SlowPath&& slow_path)
	{
		const auto function = ir.GetInsertBlock()->getParent();
		const auto check = llvm::BasicBlock::Create(ir.getContext(), "interrupt_count", function);
		const auto slow = llvm::BasicBlock::Create(ir.getContext(), "interrupt_slow", function);
		const auto done = llvm::BasicBlock::Create(ir.getContext(), "interrupt_done", function);
		const auto mask = ir.CreateAlignedLoad(ir.getInt64Ty(), events, llvm::MaybeAlign{8});
		mask->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);
		const auto busy = ir.CreateAnd(mask, ir.getInt64(std::uint64_t{busy_mask} << 32));
		ir.CreateCondBr(ir.CreateICmpNE(busy, ir.getInt64(0)), slow, check);

		ir.SetInsertPoint(check);
		ir.CreateStore(ir.getInt8(1), enabled);
		// Preserve the second atomic load after enabling: events may arrive between
		// the mask check and this load. Do not reuse the first snapshot.
		const auto count = ir.CreateAlignedLoad(ir.getInt64Ty(), events, llvm::MaybeAlign{8});
		count->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);
		const auto pending = ir.CreateAnd(count, ir.getInt64(std::uint64_t{1} << 31));
		ir.CreateCondBr(ir.CreateICmpNE(pending, ir.getInt64(0)), slow, done);

		ir.SetInsertPoint(slow);
		const auto target = slow_path();
		const auto slow_end = ir.GetInsertBlock();
		ir.CreateBr(done);
		ir.SetInsertPoint(done);
		const auto result = ir.CreatePHI(ir.getInt32Ty(), 2);
		result->addIncoming(address, check);
		result->addIncoming(target, slow_end);
		return result;
	}
}
