#pragma once

#include "GameLibrary.h"
#include "AOTError.h"
#include <string>
#include <utility>

class ppu_thread;
template <typename T>
struct ppu_module;
struct lv2_obj;

namespace rpcs3::ios::aot
{
	using native_sign_callback = std::int32_t (*)(void*, const char*, const char*, const char*, char*, std::size_t);
	void set_native_signer(native_sign_callback, void*);
	bool native_ppu_enabled();
	void register_native_ppu_range(std::uint32_t, std::uint32_t);
	void prepare_native_ppu(const ppu_module<lv2_obj>&);
	bool execute_native_ppu(ppu_thread&);
	void record_native_ppu_fallback();
	void record_native_ppu_entry();
	void configure(const std::string& cache_root, const std::string& signer, const std::string& core_digest);
	void begin_game(const installed_game& game);
	void finish_game();
	void clear_game();
	std::uint64_t cache_size(const std::string& title);
	std::uint64_t clear_cache(const std::string& title);
	void begin_preparation();
	std::string prepare(const installed_game& game);
	std::uint32_t signing_count(const std::string& identifier);
	std::pair<std::string, std::string> signing_paths(const std::string& identifier);
	void finalize(const std::string& identifier, const std::string& signed_digest);
	void cancel() noexcept;
	bool is_cancelled() noexcept;
	std::string status_json(const std::string& title_id);
} // namespace rpcs3::ios::aot
