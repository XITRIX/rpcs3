#pragma once

#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IntrinsicsAArch64.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/Analysis/ValueTracking.h>
#include <llvm/Support/KnownBits.h>
#include <algorithm>

// Exact integer builders and post-pattern-matching byte-order transforms.
// They never change a memory access, guest state update, or its ordering.
namespace spu_llvm::arm64
{
using namespace llvm;

inline Value* form_byte_mask(IRBuilder<>& ir, Value* source)
{
	const auto bytes = FixedVectorType::get(ir.getInt8Ty(), 16);
	SmallVector<int, 16> indices;
	SmallVector<Constant*, 16> masks;
	for (unsigned i = 0; i < 16; ++i)
	{
		indices.push_back(i < 8 ? 12 : 13);
		masks.push_back(ir.getInt8(1u << (i % 8)));
	}
	auto* bits = ir.CreateShuffleVector(source, indices);
	auto* mask = ConstantVector::get(masks);
	return ir.CreateSExt(ir.CreateICmpEQ(ir.CreateAnd(bits, mask), mask), bytes);
}

inline unsigned fold_single_source_tables(Function& function)
{
	SmallVector<CallInst*, 16> tables;
	for (auto& block : function)
		for (auto& instruction : block)
			if (auto* call = dyn_cast<CallInst>(&instruction); call && call->getCalledFunction())
			{
				const auto id = call->getCalledFunction()->getIntrinsicID();
				if (id == Intrinsic::aarch64_neon_tbl2 || id == Intrinsic::aarch64_neon_tbx2)
					tables.push_back(call);
			}
	unsigned changed = 0;
	for (auto* call : tables)
	{
		if (call->use_empty() || call->getType() != FixedVectorType::get(Type::getInt8Ty(function.getContext()), 16))
			continue;
		const bool extend = call->getCalledFunction()->getIntrinsicID() == Intrinsic::aarch64_neon_tbx2;
		auto* indices = call->getArgOperand(extend ? 3 : 2);
		const auto known = computeKnownBits(indices, function.getParent()->getDataLayout());
		if (!known.Zero[4] && !known.One[4])
			continue;
		IRBuilder<> ir(call);
		const bool second = known.One[4];
		// Only change the final hardware table. Keep SHUFB's special-value
		// calculation and endian handling intact. XOR leaves every invalid
		// index invalid, while mapping the selected half to indices 0..15.
		if (second)
			indices = ir.CreateXor(indices, ConstantInt::get(indices->getType(), 16));
		SmallVector<Value*, 3> args;
		if (extend)
			args.push_back(call->getArgOperand(0));
		args.push_back(call->getArgOperand(unsigned(extend) + unsigned(second)));
		args.push_back(indices);
		auto* intrinsic = Intrinsic::getOrInsertDeclaration(function.getParent(),
			extend ? Intrinsic::aarch64_neon_tbx1 : Intrinsic::aarch64_neon_tbl1, {call->getType()});
		call->replaceAllUsesWith(ir.CreateCall(intrinsic, args));
		call->eraseFromParent();
		++changed;
	}
	return changed;
}

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

inline bool byte_lanes(Type* type)
{
	const auto* vector = dyn_cast<FixedVectorType>(type);
	return vector && vector->getNumElements() == 16 && vector->getElementType()->isIntegerTy(8);
}

inline bool byte_binary(const BinaryOperator* binary)
{
	if (!byte_lanes(binary->getType()))
		return false;
	switch (binary->getOpcode())
	{
	case Instruction::Add: case Instruction::Sub: case Instruction::Mul:
	case Instruction::Shl: case Instruction::LShr: case Instruction::AShr:
		return true;
	default:
		return false;
	}
}

inline bool byte_intrinsic(const CallInst* call)
{
	if (!byte_lanes(call->getType()) || !call->getCalledFunction())
		return false;
	switch (call->getCalledFunction()->getIntrinsicID())
	{
	case Intrinsic::ctpop:
	case Intrinsic::umin: case Intrinsic::umax:
	case Intrinsic::smin: case Intrinsic::smax:
	case Intrinsic::aarch64_neon_uabd:
	case Intrinsic::aarch64_neon_urhadd:
		return true;
	default:
		return false;
	}
}

inline bool reversible_integer_predicate(const ICmpInst* compare)
{
	const auto* type = dyn_cast<FixedVectorType>(compare->getOperand(0)->getType());
	if (!type || !type->getElementType()->isIntegerTy())
		return false;
	const auto bits = type->getScalarSizeInBits();
	// Equality survives reversing bytes within a lane. Ordering does not.
	return bits * type->getNumElements() == 128 &&
		(bits == 8 || ((bits == 16 || bits == 32 || bits == 64) && compare->isEquality()));
}

inline bool byte_predicate_extension(const CastInst* cast)
{
	const auto* compare = dyn_cast<ICmpInst>(cast->getOperand(0));
	// A wide all-ones mask is byte-order invariant; a wide zero-extended
	// one is not. Keep arbitrary/shared vector predicates out of this path.
	return (isa<SExtInst>(cast) || (isa<ZExtInst>(cast) && byte_lanes(cast->getType()))) &&
		compare && compare->hasOneUse() && cast->getType() == compare->getOperand(0)->getType() &&
		reversible_integer_predicate(compare);
}

inline bool byte_minmax(const SelectInst* select)
{
	const auto* compare = dyn_cast<ICmpInst>(select->getCondition());
	if (!byte_lanes(select->getType()) || !compare || !compare->hasOneUse())
		return false;
	auto* a = compare->getOperand(0);
	auto* b = compare->getOperand(1);
	return (select->getTrueValue() == a && select->getFalseValue() == b) ||
		(select->getTrueValue() == b && select->getFalseValue() == a);
}

inline bool byte_average(Value* value, Value*& a, Value*& b)
{
	const auto* truncate = dyn_cast<TruncInst>(value);
	if (!truncate || !byte_lanes(truncate->getType()) ||
		truncate->getSrcTy() != FixedVectorType::get(Type::getInt16Ty(value->getContext()), 16))
		return false;
	const auto* shift = dyn_cast<BinaryOperator>(truncate->getOperand(0));
	auto* one = ConstantInt::get(truncate->getSrcTy(), 1);
	if (!shift || shift->getOpcode() != Instruction::LShr || shift->getOperand(1) != one || !shift->hasOneUse())
		return false;
	const auto* round = dyn_cast<BinaryOperator>(shift->getOperand(0));
	if (!round || round->getOpcode() != Instruction::Add || round->getOperand(1) != one || !round->hasOneUse())
		return false;
	const auto* sum = dyn_cast<BinaryOperator>(round->getOperand(0));
	if (!sum || sum->getOpcode() != Instruction::Add || !sum->hasOneUse())
		return false;
	const auto* lhs = dyn_cast<ZExtInst>(sum->getOperand(0));
	const auto* rhs = dyn_cast<ZExtInst>(sum->getOperand(1));
	if (!lhs || !rhs || !byte_lanes(lhs->getSrcTy()) || !byte_lanes(rhs->getSrcTy()) ||
		!lhs->hasOneUse() || !rhs->hasOneUse())
		return false;
	a = lhs->getOperand(0);
	b = rhs->getOperand(0);
	return true;
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
		Value* average_a;
		Value* average_b;
		if (byte_average(value, average_a, average_b))
			return inspect(average_a) && inspect(average_b);
		if (auto* cast = dyn_cast<BitCastInst>(value))
			return inspect(cast->getOperand(0));
		if (auto* binary = dyn_cast<BinaryOperator>(value); binary && (binary->isBitwiseLogicOp() || byte_binary(binary)))
			return inspect(binary->getOperand(0)) && inspect(binary->getOperand(1));
		if (auto* cast = dyn_cast<CastInst>(value); cast && byte_predicate_extension(cast))
			return inspect(cast->getOperand(0));
		if (auto* compare = dyn_cast<ICmpInst>(value); compare && reversible_integer_predicate(compare))
			return inspect(compare->getOperand(0)) && inspect(compare->getOperand(1));
		if (auto* call = dyn_cast<CallInst>(value); call && byte_intrinsic(call))
		{
			for (auto& operand : call->args())
				if (!inspect(operand.get()))
					return false;
			return true;
		}
		if (auto* select = dyn_cast<SelectInst>(value))
		{
			// Arbitrary vector predicates can require two instructions to
			// reverse. Only distribute through free, lane-wise predicates.
			if (!byte_minmax(select) && !free_lane_reverse(select->getCondition()))
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
	Value* average_a;
	Value* average_b;
	if (value->hasOneUse() && byte_average(value, average_a, average_b))
	{
		auto* average = Intrinsic::getOrInsertDeclaration(ir.GetInsertBlock()->getModule(),
			Intrinsic::aarch64_neon_urhadd, {value->getType()});
		return ir.CreateCall(average, {reverse_bytes(ir, average_a), reverse_bytes(ir, average_b)});
	}
	if (auto* binary = dyn_cast<BinaryOperator>(value); binary && value->hasOneUse() && (binary->isBitwiseLogicOp() || byte_binary(binary)))
	{
		auto* result = ir.CreateBinOp(binary->getOpcode(), reverse_bytes(ir, binary->getOperand(0)), reverse_bytes(ir, binary->getOperand(1)));
		if (auto* instruction = dyn_cast<Instruction>(result))
			instruction->copyIRFlags(binary);
		return result;
	}
	if (auto* cast = dyn_cast<CastInst>(value); cast && value->hasOneUse() && byte_predicate_extension(cast))
		return ir.CreateCast(cast->getOpcode(), reverse_bytes(ir, cast->getOperand(0)), cast->getType());
	if (auto* compare = dyn_cast<ICmpInst>(value); compare && value->hasOneUse() && reversible_integer_predicate(compare))
		return ir.CreateICmp(compare->getPredicate(), reverse_bytes(ir, compare->getOperand(0)), reverse_bytes(ir, compare->getOperand(1)));
	if (auto* call = dyn_cast<CallInst>(value); call && value->hasOneUse() && byte_intrinsic(call))
	{
		SmallVector<Value*, 2> args;
		for (auto& operand : call->args())
			args.push_back(reverse_bytes(ir, operand.get()));
		return ir.CreateCall(call->getCalledFunction(), args);
	}
	if (auto* select = dyn_cast<SelectInst>(value); select && value->hasOneUse())
		return ir.CreateSelect(byte_minmax(select) ? reverse_bytes(ir, select->getCondition()) : reverse_lanes(ir, select->getCondition()),
			reverse_bytes(ir, select->getTrueValue()), reverse_bytes(ir, select->getFalseValue()));
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
