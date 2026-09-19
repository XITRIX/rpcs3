#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

// Diagnostic references for the nonconstant, approximate LLVM arithmetic path.
// These are not a precise SPU hardware model or a replacement for execution.
namespace spu_float_diagnostics
{
using word = std::uint32_t;
enum class operation { fa, fs, fm, fma, fms, fnms, fi, fcgt, fcmgt, fceq, fcmeq, cflts, cfltu };

inline bool is_conversion(operation op)
{
	return op == operation::cflts || op == operation::cfltu;
}

inline word clamp(word bits)
{
	return (bits & 0x80000000u) | std::min(bits & 0x7fffffffu, 0x7f7fffffu);
}

inline bool is_nan(word bits)
{
	return (bits & 0x7fffffffu) > 0x7f800000u;
}

inline word reference(operation op, word a, word b, word c, unsigned scale = 173)
{
	if (op == operation::fi)
	{
		const word base = (b & 0x007ffc00u) << 9;
		const word product = (b & 0x3ffu) * (a & 0x7ffffu);
		const bool borrow = product > base;
		const word exponent = (b & 0xff800000u) - (borrow ? 0x00800000u : 0u);
		return clamp(exponent) | (((base - product) >> (borrow ? 8 : 9)) & 0x007fffffu);
	}

	// Match the SPU worker's flush-to-zero inputs, including signed zero.
	const auto value = [](word bits)
	{
		return std::bit_cast<float>((bits & 0x7f800000u) ? bits : bits & 0x80000000u);
	};
	const float af = value(a);
	const float bf = value(b);
	const float cf = value(c);
	if (is_conversion(op))
	{
		// LLVM skips multiplication for the identity scale. Its approximate
		// saturation predicate compares signed bit patterns, including extended
		// SPU numbers which the host represents as NaNs.
		const int exponent = 173 - static_cast<int>(scale);
		// The cached scale table contains +Inf above float's exponent range;
		// computing ldexp here under the worker's toward-zero mode gives FLT_MAX.
		const float factor = std::bit_cast<float>(exponent > 127 ? 0x7f800000u : static_cast<word>(exponent + 127) << 23);
		const float scaled = scale == 173 ? af : af * factor;
		const auto bits = std::bit_cast<std::int32_t>(scaled);
		if (op == operation::cflts)
		{
			if (bits >= 0x4f000000) return 0x7fffffffu;
			if (std::isnan(scaled) || scaled <= -0x1p31f) return 0x80000000u;
			return static_cast<word>(static_cast<std::int32_t>(scaled));
		}
		if (bits >= 0x4f800000) return 0xffffffffu;
		if (bits < 0) return 0;
		return static_cast<word>(scaled);
	}
	const word aa = a & 0x7fffffffu;
	const word ba = b & 0x7fffffffu;
	const auto mask = [](bool condition) { return word{0} - word{condition}; };
	float result = 0;
	switch (op)
	{
	case operation::fa: result = af + bf; break;
	case operation::fs: result = af - value(clamp(b)); break;
	case operation::fm: result = (af == 0 || bf == 0) ? 0 : af * bf; break;
	case operation::fma: result = (af == 0 || bf == 0) ? cf : std::fma(af, bf, cf); break;
	case operation::fms: result = std::fma(value(clamp(a)), value(clamp(b)), is_nan(c) ? cf : -cf); break;
	case operation::fnms: result = std::fma(-value(clamp(a)), value(clamp(b)), cf); break;
	case operation::fcgt:
	{
		const auto ai = std::bit_cast<std::int32_t>(a);
		const auto bi = std::bit_cast<std::int32_t>(b);
		return mask(af != bf && ((ai & bi) >= 0 ? ai > bi : ai < bi));
	}
	case operation::fcmgt: return mask((is_nan(a) || is_nan(b) || std::fabs(af) > std::fabs(bf)) && aa > ba);
	case operation::fceq: return mask(af == bf || a == b);
	case operation::fcmeq: return mask(std::fabs(af) == std::fabs(bf) || aa == ba);
	case operation::fi: break;
	case operation::cflts: case operation::cfltu: break;
	}
	const word bits = std::bit_cast<word>(result);
	return (bits & 0x7f800000u) ? bits : bits & 0x80000000u;
}

inline bool finite_inputs(operation op, word a, word b, word c)
{
	// Conversion errors have their own integer interpretation and must remain
	// visible even when a source is an extended SPU number.
	if (is_conversion(op)) return true;
	const auto finite = [](word x) { return (x & 0x7f800000u) != 0x7f800000u; };
	return finite(a) && finite(b) && ((op != operation::fma && op != operation::fms && op != operation::fnms) || finite(c));
}

inline unsigned refinement_boundary(word a, word b)
{
	const word magnitude = a & 0x7fffffffu;
	if (magnitude && magnitude < 0x00800000u) return 1;
	// FRSQEST returns a positive estimate even for a negative input. Such an
	// FI can match LLVM's sqrt(abs(x)) replacement later in the block.
	if ((a & 0x80000000u) && magnitude >= 0x00800000u && magnitude < 0x7f800000u && !(b & 0x80000000u)) return 2;
	if (magnitude >= 0x7e800000u && magnitude < 0x7f800000u && !(b & 0x7f800000u)) return 4;
	return 0;
}

inline bool significant(word actual, word expected)
{
	if (actual == expected || ((actual | expected) & 0x7fffffffu) == 0)
		return false;
	// Native NaN payload selection can differ without changing classification.
	if (is_nan(actual) && is_nan(expected))
		return false;
	if (((actual & 0x7f800000u) == 0x7f800000u) != ((expected & 0x7f800000u) == 0x7f800000u))
		return true;
	if (((actual ^ expected) & 0x80000000u) || is_nan(actual) || is_nan(expected))
		return true;
	// Count small rounding differences separately; do not let FI's known
	// one-ULP differences consume the bounded operand-report budget.
	return (actual > expected ? actual - expected : expected - actual) > 16;
}
}
