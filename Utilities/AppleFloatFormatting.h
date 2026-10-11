#pragma once

#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__APPLE__)
#include <xlocale.h>

namespace utils
{
// libc++'s floating to_chars is only present from iOS 16.3. Try increasing
// fixed precision until the decimal round-trips, retaining the native 32-byte
// bound and C locale. Unlike %f's default precision this preserves tiny values.
inline std::to_chars_result legacy_apple_float_to_chars(char* first, char* last, double value)
{
	const auto bits = std::bit_cast<std::uint64_t>(value);
	if (std::isnan(value))
	{
		// Preserve libc++'s spellings, including signaling and indefinite NaNs.
		const bool negative = bits >> 63;
		const char* text = (bits & 0x0008000000000000ull) == 0 ? (negative ? "-nan(snan)" : "nan(snan)") :
			bits == 0xfff8000000000000ull ? "-nan(ind)" : negative ? "-nan" : "nan";
		const auto size = std::strlen(text);
		if (size > static_cast<std::size_t>(last - first)) return {last, std::errc::value_too_large};
		std::memcpy(first, text, size);
		return {first + size, {}};
	}
	static locale_t c_locale = newlocale(LC_NUMERIC_MASK, "C", nullptr);
	if (!c_locale)
	{
		return {last, std::errc::invalid_argument};
	}

	const auto capacity = static_cast<std::size_t>(last - first);
	std::array<char, 33> buffer{};
	for (int precision = 0; precision <= 32; ++precision)
	{
		const int size = snprintf_l(buffer.data(), buffer.size(), c_locale, "%.*f", precision, value);
		if (size < 0 || static_cast<std::size_t>(size) > capacity || static_cast<std::size_t>(size) >= buffer.size())
		{
			return {last, std::errc::value_too_large};
		}
		char* end = nullptr;
		const double parsed = strtod_l(buffer.data(), &end, c_locale);
		if (end == buffer.data() + size &&
			(std::bit_cast<std::uint64_t>(parsed) == std::bit_cast<std::uint64_t>(value) ||
			 (std::isnan(parsed) && std::isnan(value))))
		{
			std::memcpy(first, buffer.data(), size);
			return {first + size, {}};
		}
	}
	return {last, std::errc::value_too_large};
}
}
#endif

namespace utils
{
inline std::to_chars_result float_to_chars_fixed(char* first, char* last, double value)
{
#if defined(__APPLE__) && defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__ < 160300
	// libc++ marks this overload unavailable at compile time for older floors,
	// so even an availability guard cannot safely reference its symbol.
	return legacy_apple_float_to_chars(first, last, value);
#else
	return std::to_chars(first, last, value, std::chars_format::fixed);
#endif
}
}
