#pragma once

#include "SPUARM64Lowering.h"
#include <llvm/ADT/MapVector.h>

namespace spu_llvm::arm64
{
// A full register reversal is a lane reversal followed by a byte swap inside
// each lane. Lane-wise comparisons commute with the former, but NOT the latter.
// Keep the required REV16/REV32 on the inputs and avoid reversing the output
// mask. Restrict the graph to tested comparison and carry/borrow shapes; do not
// duplicate live/shared expressions or move any memory operation.
inline unsigned fold_wide_comparison_reversals(Function& function)
{
	SmallVector<CallInst*, 16> outputs;
	for (auto& block : function)
		for (auto& instruction : block)
			if (auto* lookup = table_lookup(&instruction); lookup && is_reverse(lookup->getArgOperand(1)))
				outputs.push_back(lookup);

	unsigned changed = 0;
	for (auto* output : outputs)
	{
		if (output->use_empty())
			continue;
		auto* bytes = dyn_cast<BitCastInst>(output->getArgOperand(0));
		auto* extend = bytes ? dyn_cast<CastInst>(bytes->getOperand(0)) : nullptr;
		if (!extend || (!isa<SExtInst>(extend) && !isa<ZExtInst>(extend)))
			continue;
		auto* compare = dyn_cast<ICmpInst>(extend->getOperand(0));
		auto* type = dyn_cast<FixedVectorType>(extend->getType());
		if (!compare || !type || !type->getElementType()->isIntegerTy() ||
			compare->getOperand(0)->getType() != type)
			continue;
		const unsigned bits = type->getScalarSizeInBits();
		if ((bits != 16 && bits != 32) || bits * type->getNumElements() != 128)
			continue;
		const bool zero_extend = isa<ZExtInst>(extend);
		const auto predicate = compare->getPredicate();
		if (zero_extend ? (bits != 32 || (predicate != ICmpInst::ICMP_ULT && predicate != ICmpInst::ICMP_ULE)) :
			(predicate != ICmpInst::ICMP_SGT && predicate != ICmpInst::ICMP_UGT))
			continue;

		// CG uses (a + b) < a; BG uses a <= b. Other arithmetic graphs are
		// deliberately excluded: the screen did not establish a useful win.
		BinaryOperator* carry_sum = nullptr;
		if (zero_extend && predicate == ICmpInst::ICMP_ULT)
		{
			carry_sum = dyn_cast<BinaryOperator>(compare->getOperand(0));
			if (!carry_sum || carry_sum->getOpcode() != Instruction::Add ||
				compare->getOperand(1) != carry_sum->getOperand(0))
				continue;
		}

		SmallPtrSet<Instruction*, 16> nodes{output, bytes, extend, compare};
		SmallMapVector<Value*, CallInst*, 4> inputs;
		auto inspect = [&](Value* value)
		{
			if (value->getType() != type)
				return false;
			if (isa<Constant>(value))
				return true;
			auto* cast = dyn_cast<BitCastInst>(value);
			auto* lookup = cast ? table_lookup(cast->getOperand(0)) : nullptr;
			if (!lookup || !is_reverse(lookup->getArgOperand(1)))
				return false;
			nodes.insert(cast);
			nodes.insert(lookup);
			inputs.insert({value, lookup});
			return true;
		};
		if (carry_sum)
		{
			nodes.insert(carry_sum);
			if (!inspect(carry_sum->getOperand(0)) || !inspect(carry_sum->getOperand(1)))
				continue;
		}
		else if (!inspect(compare->getOperand(0)) || !inspect(compare->getOperand(1)))
			continue;
		if (inputs.empty())
			continue;
		bool shared = false;
		for (auto* node : nodes)
			if (node != output)
				for (auto* user : node->users())
					if (!nodes.contains(dyn_cast<Instruction>(user)))
						shared = true;
		if (shared)
			continue;

		IRBuilder<> ir(output);
		SmallMapVector<Value*, Value*, 4> converted;
		auto* swap = Intrinsic::getOrInsertDeclaration(function.getParent(), Intrinsic::bswap, {type});
		for (const auto& [value, lookup] : inputs)
			converted.insert({value, ir.CreateCall(swap, {ir.CreateBitCast(lookup->getArgOperand(0), type)})});
		auto lane_reverse = [&](Value* value) -> Value*
		{
			if (isa<Constant>(value))
				return ir.CreateShuffleVector(value, reverse_lanes_mask(type));
			return converted.lookup(value);
		};
		Value* lhs;
		Value* rhs;
		if (carry_sum)
		{
			rhs = lane_reverse(carry_sum->getOperand(0));
			lhs = ir.CreateAdd(rhs, lane_reverse(carry_sum->getOperand(1)));
			if (auto* instruction = dyn_cast<Instruction>(lhs))
				instruction->copyIRFlags(carry_sum);
		}
		else
		{
			lhs = lane_reverse(compare->getOperand(0));
			rhs = lane_reverse(compare->getOperand(1));
		}
		Value* result = ir.CreateSExt(ir.CreateICmp(predicate, lhs, rhs), type);
		if (zero_extend)
			result = ir.CreateAnd(result, ConstantInt::get(type, 1ull << (bits - 8)));
		output->replaceAllUsesWith(ir.CreateBitCast(result, output->getType()));
		output->eraseFromParent();
		++changed;
	}
	return changed;
}
}
