#pragma once

#include "Emu/CPU/Hypervisor.h"
#include <array>
#include <exception>

namespace rpcs3::ios::aot
{
	// Per-invocation dispatch ownership; nested CPU callbacks restore the prior
	// resolver with the gateway frame. The owner retains returned signed images.
	struct native_spu_dispatch
	{
		void* owner;
		void* (*resolve)(void* owner, void* cpu, void* local_store);
	};

	// Internal native execution ABI, independent of the public signed-module ABI.
	// All target and CPU pointers are runtime data. No executable allocation occurs.
	void invoke_native(void* entry, const std::array<u64, 7>& arguments, void* cpu,
		rpcs3::hypervisor_context_t& hypervisor, const native_spu_dispatch* dispatch = nullptr);
	bool escape_native_if_active(void* cpu);
	void capture_native_exception(std::exception_ptr error);
	[[noreturn]] void escape_native_exception();

	// Catch at the prebuilt C++ import boundary, before an exception reaches a GHC
	// frame. Leave the catch normally before escaping, so __cxa_end_catch and all
	// helper-local destructors run. Rethrow only after the gateway has returned.
	template <auto Function, typename Signature = decltype(Function)>
	struct native_import;
	template <auto Function, typename R, typename... A>
	struct native_import<Function, R (*)(A...)>
	{
		static R call(A... arguments)
		{
			try
			{
				return Function(arguments...);
			}
			catch (...)
			{
				capture_native_exception(std::current_exception());
			}
			escape_native_exception();
		}
	};
	template <auto Function, typename R, typename... A>
	struct native_import<Function, R (*)(A...) noexcept> : native_import<Function, R (*)(A...)>
	{
	};
} // namespace rpcs3::ios::aot

extern "C" void rpcs3_aot_native_exit();
extern "C" [[noreturn]] void rpcs3_aot_native_restore(u64 scratch);

extern "C" void rpcs3_aot_native_spu_dispatch();
extern "C" void rpcs3_aot_native_spu_reject();
extern "C" void rpcs3_aot_native_ppu_fallback();
