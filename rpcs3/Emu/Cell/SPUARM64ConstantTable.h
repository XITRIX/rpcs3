#pragma once
#include <array>
#include "SPUARM64Lowering.h"
namespace spu_llvm::arm64
{
	inline bool read_constant_table_indices(llvm::Value* value, std::array<unsigned, 16>& out)
	{
		auto* constant = llvm::dyn_cast<llvm::Constant>(value);
		if (!constant || constant->getType() != llvm::FixedVectorType::get(llvm::Type::getInt8Ty(value->getContext()), 16))
			return false;
		for (unsigned i = 0; i < 16; ++i)
		{
			auto* lane = llvm::dyn_cast_or_null<llvm::ConstantInt>(constant->getAggregateElement(i));
			if (!lane)
				return false;
			out[i] = lane->getZExtValue();
		}
		return true;
	}
	// Fold only fully specified constant TBL1 masks. Invalid indices still select
	// zero. Compose a nested table only when it has one use; shared expressions
	// retain their existing lifetime and work. No memory or guest-state operation
	// is moved. Run after the existing reversal pass, which can produce identity
	// table lookups that LLVM does not remove itself.
	inline unsigned fold_constant_tables(llvm::Function& function)
	{
		llvm::SmallVector<llvm::CallInst*, 32> calls;
		for (auto& block : function)
			for (auto& instruction : block)
				if (auto* call = table_lookup(&instruction))
					calls.push_back(call);
		unsigned changes = 0;
		for (auto* call : calls)
		{
			if (call->use_empty())
				continue;
			std::array<unsigned, 16> indices, inner_indices;
			if (!read_constant_table_indices(call->getArgOperand(1), indices))
				continue;
			auto* source = call->getArgOperand(0);
			bool composed = false;
			if (auto* inner = table_lookup(source); inner && inner->hasOneUse() && read_constant_table_indices(inner->getArgOperand(1), inner_indices))
			{
				for (auto& lane : indices)
					lane = lane < 16 ? inner_indices[lane] : 255;
				source = inner->getArgOperand(0);
				composed = true;
			}
			bool identity = true, zero = true;
			for (unsigned i = 0; i < 16; ++i)
			{
				identity &= indices[i] == i;
				zero &= indices[i] >= 16;
			}
			llvm::IRBuilder<> ir(call);
			llvm::Value* result = nullptr;
			if (identity)
				result = source;
			else if (zero)
				result = llvm::Constant::getNullValue(call->getType());
			else if (composed)
			{
				llvm::SmallVector<llvm::Constant*, 16> mask;
				for (unsigned lane : indices)
					mask.push_back(ir.getInt8(lane));
				result = ir.CreateCall(call->getCalledFunction(), {source, llvm::ConstantVector::get(mask)});
			}
			if (result)
			{
				call->replaceAllUsesWith(result);
				call->eraseFromParent();
				++changes;
			}
		}
		return changes;
	}
} // namespace spu_llvm::arm64
