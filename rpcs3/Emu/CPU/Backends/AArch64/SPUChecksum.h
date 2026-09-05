#pragma once

#include <array>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/IntrinsicsAArch64.h>

namespace aarch64
{
	// The four 128-bit XOR differences are zero iff the SPU checksum matches.
	// Keep their reduction in NEON instead of extracting eight scalar lanes.
	// Byte-wise unsigned maximum is zero iff every input bit is zero; unlike
	// addition or XOR across parts, neither OR nor maximum can cancel changes.
	inline llvm::Value* spu_checksum_mismatch(llvm::IRBuilder<>& ir, const std::array<llvm::Value*, 4>& differences)
	{
		const auto lo = ir.CreateOr(differences[0], differences[1]);
		const auto hi = ir.CreateOr(differences[2], differences[3]);
		const auto combined = ir.CreateOr(lo, hi);
		const auto bytes_type = llvm::FixedVectorType::get(ir.getInt8Ty(), 16);
		const auto bytes = ir.CreateBitCast(combined, bytes_type);
		const auto umaxv = llvm::Intrinsic::getOrInsertDeclaration(
			ir.GetInsertBlock()->getModule(), llvm::Intrinsic::aarch64_neon_umaxv, {ir.getInt32Ty(), bytes_type});
		const auto maximum = ir.CreateCall(umaxv, {bytes});
		return ir.CreateICmpNE(maximum, ir.getInt32(0));
	}
}
