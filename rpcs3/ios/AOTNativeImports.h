#pragma once

#include "AOTNativeGateway.h"
#include <cstdint>
#include <string_view>

namespace rpcs3::ios::aot
{
	bool register_native_import(std::string_view key, std::uintptr_t address);
	std::uintptr_t resolve_native_import(std::string_view key);

	// The exact core build owns these identifiers. They carry no process address
	// into the file: the cold loader resolves them to its prebuilt guarded helpers.
	// Retain each instantiated registration in the core's initialization list so
	// loading a prepared image does not require running the compiler first.
	template <auto Function, auto Name = 0>
	struct registered_native_import
	{
		static constexpr std::string_view key()
		{
			return __PRETTY_FUNCTION__;
		}
		[[gnu::used]] inline static const bool registered = register_native_import(
			key(), reinterpret_cast<std::uintptr_t>(&native_import<Function>::call));
		static auto address()
		{
			(void)registered;
			return &native_import<Function>::call;
		}
	};
} // namespace rpcs3::ios::aot
