#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace jit_profile
{
	struct limits
	{
		std::size_t records = 65536;
		std::size_t map_bytes = 8 * 1024 * 1024;
		std::size_t code_bytes = 32 * 1024 * 1024;
		std::size_t function_bytes = 256 * 1024;
	};

	// Diagnostic output only. Never used to load or execute cached code.
	class writer
	{
		limits m_limits;
		std::atomic<bool> m_enabled{false};
		std::mutex m_mutex;
		std::FILE* m_map = nullptr;
		std::FILE* m_code = nullptr;
		std::size_t m_records = 0;
		std::size_t m_map_bytes = 0;
		std::size_t m_code_bytes = 0;
		std::uintptr_t m_code_base = 0;
		std::size_t m_code_capacity = 0;

	public:
		explicit writer(limits budget = {}) : m_limits(budget) {}
		~writer()
		{
			if (m_map) std::fclose(m_map);
			if (m_code) std::fclose(m_code);
		}

		bool enabled() const noexcept { return m_enabled.load(std::memory_order_relaxed); }

		bool open(const std::string& stem, std::uintptr_t code_base = 0,
			std::size_t code_capacity = std::numeric_limits<std::uintptr_t>::max())
		{
			std::lock_guard lock(m_mutex);
			if (m_map || m_code) return false;
			m_map = std::fopen((stem + ".map").c_str(), "wbx");
			if (!m_map) return false;
			m_code = std::fopen((stem + ".bin").c_str(), "wbx");
			if (!m_code)
			{
				std::fclose(m_map);
				m_map = nullptr;
				std::remove((stem + ".map").c_str());
				return false;
			}
			m_code_base = code_base;
			m_code_capacity = code_capacity;
			m_enabled.store(true, std::memory_order_relaxed);
			return true;
		}

		// Call only after successful JIT finalization, while this compiler still
		// owns the symbol's RX storage. Later branch patches are not snapshots.
		void append(std::uintptr_t address, std::size_t size, std::string_view name)
		{
			if (!enabled() || !address || !size) return;
			std::lock_guard lock(m_mutex);
			if (!enabled()) return;
			if (address < m_code_base || size > m_code_capacity || address - m_code_base > m_code_capacity - size) return;
			if (m_records == m_limits.records)
			{
				m_enabled.store(false, std::memory_order_relaxed);
				return;
			}

			std::array<char, 257> label{};
			for (std::size_t i = 0; i < std::min(name.size(), label.size() - 1); i++)
			{
				const auto c = static_cast<unsigned char>(name[i]);
				label[i] = c > 32 && c < 127 ? static_cast<char>(c) : '_';
			}
			const auto captured = size <= m_limits.function_bytes && size <= m_limits.code_bytes - m_code_bytes ? size : 0;
			const auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
			std::array<char, 512> line{};
			// v1: RX address, full size, blob offset, captured size, monotonic ns, name.
			const int count = std::snprintf(line.data(), line.size(), "%llx %llx %llx %llx %llu %s\n",
				static_cast<unsigned long long>(address), static_cast<unsigned long long>(size),
				static_cast<unsigned long long>(m_code_bytes), static_cast<unsigned long long>(captured),
				static_cast<unsigned long long>(timestamp), label.data());
			if (count <= 0 || static_cast<std::size_t>(count) >= line.size() ||
				static_cast<std::size_t>(count) > m_limits.map_bytes - m_map_bytes)
			{
				m_enabled.store(false, std::memory_order_relaxed);
				return;
			}

			if ((captured && std::fwrite(reinterpret_cast<const void*>(address), 1, captured, m_code) != captured) ||
				std::fwrite(line.data(), 1, static_cast<std::size_t>(count), m_map) != static_cast<std::size_t>(count))
			{
				m_enabled.store(false, std::memory_order_relaxed);
				return;
			}
			m_records++;
			m_map_bytes += static_cast<std::size_t>(count);
			m_code_bytes += captured;
		}

		void flush()
		{
			std::lock_guard lock(m_mutex);
			const bool map_ok = !m_map || std::fflush(m_map) == 0;
			const bool code_ok = !m_code || std::fflush(m_code) == 0;
			if (!map_ok || !code_ok) m_enabled.store(false, std::memory_order_relaxed);
		}
	};

	inline writer& spu_writer()
	{
		static writer instance;
		return instance;
	}

	class pending_batch
	{
		struct symbol
		{
			std::uintptr_t address;
			std::size_t size;
			std::string name;
		};
		std::vector<symbol> m_symbols;

	public:
		void add(std::uintptr_t address, std::size_t size, std::string_view name)
		{
			if (m_symbols.size() < 4096)
			{
				m_symbols.push_back({address, size, std::string{name.substr(0, 256)}});
			}
		}

		void complete(writer& output, bool success)
		{
			if (success && !m_symbols.empty())
			{
				for (const auto& symbol : m_symbols) output.append(symbol.address, symbol.size, symbol.name);
				output.flush();
			}
			m_symbols.clear();
		}
	};
}
