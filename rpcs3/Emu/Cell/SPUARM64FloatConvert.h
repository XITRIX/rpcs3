#pragma once

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>

namespace spu_llvm::arm64
{
// Approximate xfloat conversion after scaling. Clamp integer encodings before
// converting so positive NaNs saturate high and negative NaNs saturate low.
// The saturating intrinsic is essential: +2^31/+2^32 are outside FPTo[SU]I's
// defined range even though AArch64's native instruction saturates them.
inline llvm::Value* convert_scaled_float(llvm::IRBuilder<>& ir, llvm::Value* value, bool is_signed)
{
	auto* integers = llvm::FixedVectorType::get(ir.getInt32Ty(), 4);
	auto* bits = ir.CreateBitCast(value, integers);
	auto* first = llvm::Intrinsic::getOrInsertDeclaration(ir.GetInsertBlock()->getModule(),
		is_signed ? llvm::Intrinsic::smin : llvm::Intrinsic::smax, {integers});
	bits = ir.CreateCall(first, {bits, llvm::ConstantInt::get(integers, is_signed ? 0x4f000000 : 0)});
	auto* minimum = llvm::Intrinsic::getOrInsertDeclaration(ir.GetInsertBlock()->getModule(),
		llvm::Intrinsic::umin, {integers});
	bits = ir.CreateCall(minimum, {bits, llvm::ConstantInt::get(integers, is_signed ? 0xcf000000 : 0x4f800000)});
	auto* conversion = llvm::Intrinsic::getOrInsertDeclaration(ir.GetInsertBlock()->getModule(),
		is_signed ? llvm::Intrinsic::fptosi_sat : llvm::Intrinsic::fptoui_sat, {integers, value->getType()});
	return ir.CreateCall(conversion, {ir.CreateBitCast(bits, value->getType())});
}

}
