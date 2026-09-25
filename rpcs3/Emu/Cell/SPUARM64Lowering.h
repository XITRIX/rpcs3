#pragma once

#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicsAArch64.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <algorithm>

// Run after SPU pattern matching. These transforms only permute existing SSA
// values: they never change a memory access, guest state update, or its ordering.
namespace spu_llvm::arm64
{
using namespace llvm;

inline Value* shuffle_with_splat(IRBuilder<>& ir, Value* source, Value* selector, Value* byte_indices, unsigned splat, bool constant_a)
{
	// SHUFB's high-nibble classes encode both the constant source and
	// 0/FF/80 special values. TBX then reads only the variable register.
	const auto bytes = FixedVectorType::get(ir.getInt8Ty(), 16);
	SmallVector<Constant*, 16> lanes;
	for (unsigned i = 0; i < 8; ++i)
		lanes.push_back(ir.getInt8((i % 2 == 0) == constant_a ? splat : 0));
	for (unsigned value : {0, 0, 0, 0, 255, 255, 128, 128})
		lanes.push_back(ir.getInt8(value));
	auto* module = ir.GetInsertBlock()->getModule();
	auto* tbl = Intrinsic::getOrInsertDeclaration(module, Intrinsic::aarch64_neon_tbl1, {bytes});
	auto* tbx = Intrinsic::getOrInsertDeclaration(module, Intrinsic::aarch64_neon_tbx1, {bytes});
	auto* base = ir.CreateCall(tbl, {ConstantVector::get(lanes), ir.CreateLShr(selector, 4)});
	auto* indices = constant_a ? ir.CreateXor(byte_indices, ConstantInt::get(bytes, 16)) : byte_indices;
	return ir.CreateCall(tbx, {base, source, ir.CreateAnd(indices, ConstantInt::get(bytes, 159))});
}

inline CallInst* table_lookup(Value* value)
{
	auto* call = dyn_cast<CallInst>(value);
	return call && call->getCalledFunction() &&
		call->getCalledFunction()->getIntrinsicID() == Intrinsic::aarch64_neon_tbl1 ? call : nullptr;
}

inline bool is_reverse(Value* indices)
{
	auto* constant = dyn_cast<Constant>(indices);
	if (!constant || constant->getType() != FixedVectorType::get(Type::getInt8Ty(indices->getContext()), 16))
		return false;
	for (unsigned i = 0; i < 16; ++i)
	{
		auto* lane = dyn_cast_or_null<ConstantInt>(constant->getAggregateElement(i));
		if (!lane || lane->getZExtValue() != 15 - i)
			return false;
	}
	return true;
}

inline SmallVector<int, 16> reverse_lanes_mask(Type* type)
{
	SmallVector<int, 16> mask;
	const auto count = cast<FixedVectorType>(type)->getNumElements();
	for (unsigned i = count; i > 0; --i)
		mask.push_back(i - 1);
	return mask;
}

inline bool is_splat(Value* value)
{
	if (auto* constant = dyn_cast<Constant>(value))
		return constant->getType()->isVectorTy() && constant->getSplatValue();
	if (auto* shuffle = dyn_cast<ShuffleVectorInst>(value))
	{
		const auto mask = shuffle->getShuffleMask();
		return !mask.empty() && mask.front() >= 0 && std::all_of(mask.begin(), mask.end(), [&](int lane) { return lane == mask.front(); });
	}
	return false;
}

inline bool free_lane_reverse(Value* value, unsigned depth = 0)
{
	if (!value->getType()->isVectorTy())
		return true;
	if (depth > 8)
		return false;
	if (isa<Constant>(value) || is_splat(value))
		return true;
	if (!value->hasOneUse())
		return false;
	if (auto* binary = dyn_cast<BinaryOperator>(value); binary && binary->getType()->isIntOrIntVectorTy())
		return free_lane_reverse(binary->getOperand(0), depth + 1) && free_lane_reverse(binary->getOperand(1), depth + 1);
	if (auto* compare = dyn_cast<ICmpInst>(value))
		return free_lane_reverse(compare->getOperand(0), depth + 1) && free_lane_reverse(compare->getOperand(1), depth + 1);
	return false;
}

inline Value* reverse_lanes(IRBuilder<>& ir, Value* value)
{
	if (!value->getType()->isVectorTy() || is_splat(value))
		return value;
	if (auto* binary = dyn_cast<BinaryOperator>(value); binary && free_lane_reverse(value))
		return ir.CreateBinOp(binary->getOpcode(), reverse_lanes(ir, binary->getOperand(0)), reverse_lanes(ir, binary->getOperand(1)));
	if (auto* compare = dyn_cast<ICmpInst>(value); compare && free_lane_reverse(value))
		return ir.CreateICmp(compare->getPredicate(), reverse_lanes(ir, compare->getOperand(0)), reverse_lanes(ir, compare->getOperand(1)));
	return ir.CreateShuffleVector(value, reverse_lanes_mask(value->getType()));
}

struct reverse_cost
{
	unsigned removed = 1; // The outer byte-reversal lookup.
	unsigned added = 0;
	unsigned visited = 0;
	SmallPtrSet<Value*, 16> seen;
	bool inspect(Value* value)
	{
		if (!seen.insert(value).second)
			return true;
		if (++visited > 32)
			return false;
		if (isa<Constant>(value))
			return true;
		// Do not duplicate a shared expression or leave its original work
		// live. A shared byte reversal itself can still be read through.
		if (auto* lookup = table_lookup(value); lookup && is_reverse(lookup->getArgOperand(1)))
		{
			removed += lookup->hasOneUse();
			return true;
		}
		if (!value->hasOneUse())
		{
			const auto* type = dyn_cast<FixedVectorType>(value->getType());
			if (type && type->getElementType()->isIntegerTy(8) && is_splat(value))
				return true;
			++added;
			return true;
		}
		if (auto* cast = dyn_cast<BitCastInst>(value))
			return inspect(cast->getOperand(0));
		if (auto* binary = dyn_cast<BinaryOperator>(value); binary && binary->isBitwiseLogicOp())
			return inspect(binary->getOperand(0)) && inspect(binary->getOperand(1));
		if (auto* select = dyn_cast<SelectInst>(value))
		{
			// Arbitrary vector predicates can require two instructions to
			// reverse. Only distribute through free, lane-wise predicates.
			if (!free_lane_reverse(select->getCondition()))
				return false;
			return inspect(select->getTrueValue()) && inspect(select->getFalseValue());
		}
		const auto* type = dyn_cast<FixedVectorType>(value->getType());
		if (type && type->getElementType()->isIntegerTy(8) && is_splat(value))
			return true;
		// A single-use shuffle can compose its fixed lane selection with this
		// reversal instead of materializing a second intermediate shuffle.
		if (isa<ShuffleVectorInst>(value) && value->hasOneUse())
		{
			const auto lane_bits = value->getType()->getScalarSizeInBits();
			if (lane_bits >= 8 && lane_bits % 8 == 0)
				return true;
		}
		++added;
		return true;
	}
};

inline Value* reverse_bytes(IRBuilder<>& ir, Value* value)
{
	if (auto* lookup = table_lookup(value); lookup && is_reverse(lookup->getArgOperand(1)))
		return lookup->getArgOperand(0);
	if (auto* cast = dyn_cast<BitCastInst>(value); cast && value->hasOneUse())
		return ir.CreateBitCast(reverse_bytes(ir, cast->getOperand(0)), value->getType());
	if (auto* binary = dyn_cast<BinaryOperator>(value); binary && value->hasOneUse() && binary->isBitwiseLogicOp())
		return ir.CreateBinOp(binary->getOpcode(), reverse_bytes(ir, binary->getOperand(0)), reverse_bytes(ir, binary->getOperand(1)));
	if (auto* select = dyn_cast<SelectInst>(value); select && value->hasOneUse())
		return ir.CreateSelect(reverse_lanes(ir, select->getCondition()), reverse_bytes(ir, select->getTrueValue()), reverse_bytes(ir, select->getFalseValue()));
	const auto bytes = FixedVectorType::get(ir.getInt8Ty(), 16);
	if (value->getType() == bytes && is_splat(value))
		return value;
	if (auto* shuffle = dyn_cast<ShuffleVectorInst>(value); shuffle && value->hasOneUse())
	{
		// Compose at byte granularity here: the SPU pipeline deliberately
		// has no InstCombine pass to merge two generic shufflevectors.
		const auto lane_bits = shuffle->getType()->getScalarSizeInBits();
		if (lane_bits >= 8 && lane_bits % 8 == 0)
		{
			const auto lane_bytes = lane_bits / 8;
			const auto input_bytes = cast<FixedVectorType>(shuffle->getOperand(0)->getType())->getNumElements() * lane_bytes;
			const auto input_type = FixedVectorType::get(ir.getInt8Ty(), input_bytes);
			SmallVector<int, 16> mask;
			for (unsigned i = 0; i < 16; ++i)
			{
				const int lane = shuffle->getMaskValue((15 - i) / lane_bytes);
				mask.push_back(lane < 0 ? -1 : lane * lane_bytes + (15 - i) % lane_bytes);
			}
			return ir.CreateBitCast(ir.CreateShuffleVector(ir.CreateBitCast(shuffle->getOperand(0), input_type), ir.CreateBitCast(shuffle->getOperand(1), input_type), mask), value->getType());
		}
	}
	auto* raw = ir.CreateBitCast(value, bytes);
	if (isa<Constant>(value))
		return ir.CreateBitCast(reverse_lanes(ir, raw), value->getType());
	const auto intrinsic = Intrinsic::getOrInsertDeclaration(ir.GetInsertBlock()->getModule(), Intrinsic::aarch64_neon_tbl1, {bytes});
	SmallVector<Constant*, 16> lanes;
	for (unsigned i = 16; i > 0; --i)
		lanes.push_back(ir.getInt8(i - 1));
	return ir.CreateBitCast(ir.CreateCall(intrinsic, {raw, ConstantVector::get(lanes)}), value->getType());
}

inline unsigned fold_byte_reversals(Function& function)
{
	SmallVector<CallInst*, 32> lookups;
	for (auto& block : function)
		for (auto& instruction : block)
			if (auto* lookup = table_lookup(&instruction))
				lookups.push_back(lookup);
	unsigned changed = 0;
	for (auto* lookup : lookups)
	{
		if (lookup->use_empty())
			continue;
		IRBuilder<> ir(lookup);
		auto* data = lookup->getArgOperand(0);
		auto* indices = lookup->getArgOperand(1);
		Value* result = nullptr;
		if (is_reverse(indices))
		{
			if (auto* inner = table_lookup(data); inner && inner->hasOneUse() && free_lane_reverse(inner->getArgOperand(1)))
				result = ir.CreateCall(inner->getCalledFunction(), {inner->getArgOperand(0), reverse_lanes(ir, inner->getArgOperand(1))});
			else if (reverse_cost cost; cost.inspect(data) && cost.removed > cost.added)
				result = reverse_bytes(ir, data);
		}
		else if (auto* inner = table_lookup(data); inner && inner->hasOneUse() && is_reverse(inner->getArgOperand(1)))
		{
			// XOR only the low nibble. Out-of-range TBL indices stay out of
			// range, including bit-7 selectors, so their zero result is kept.
			result = ir.CreateCall(lookup->getCalledFunction(), {inner->getArgOperand(0), ir.CreateXor(indices, ConstantVector::getSplat(ElementCount::getFixed(16), ir.getInt8(15)))});
		}
		if (result)
		{
			lookup->replaceAllUsesWith(result);
			lookup->eraseFromParent();
			++changed;
		}
	}
	return changed;
}
}
