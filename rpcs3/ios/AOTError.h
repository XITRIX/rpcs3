#pragma once
#include <string>

namespace rpcs3::ios::aot
{
	// Keep validation failures independent of standard-library exception RTTI.
	// LLVM archives compiled without RTTI may coalesce incomplete base metadata
	// for std::runtime_error on Darwin. This payload crosses only the core boundary.
	struct failure final
	{
		std::string message;
	};
} // namespace rpcs3::ios::aot
