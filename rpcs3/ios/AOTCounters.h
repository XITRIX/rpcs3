#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace rpcs3::ios::aot
{
	// CPU workers normally own different cache lines. Slots wrap safely if more
	// than 64 workers are created: atomics preserve exact totals on collisions.
	// This bounded storage also retains counts after a worker exits.
	class runtime_counters
	{
		struct alignas(128) shard
		{
			std::array<std::atomic<std::uint64_t>, 4> values{};
		};
		std::array<shard, 64> shards{};

		static unsigned next_worker_slot() noexcept
		{
			static std::atomic<unsigned> next{0};
			return next.fetch_add(1, std::memory_order_relaxed) % 64;
		}
		static unsigned worker_slot() noexcept
		{
			thread_local const unsigned slot = next_worker_slot();
			return slot;
		}

	public:
		class worker
		{
			friend class runtime_counters;
			std::array<std::atomic<std::uint64_t>, 4>* values;
			explicit worker(shard& slot) noexcept : values(&slot.values) {}

		public:
			std::atomic<std::uint64_t>* counter(unsigned column) const noexcept
			{
				return &(*values)[column];
			}
			void add(unsigned column, std::uint64_t count = 1) const noexcept
			{
				(*values)[column].fetch_add(count, std::memory_order_relaxed);
			}
		};
		// Cache this process-lifetime handle alongside dispatch state. Incrementing
		// it needs neither another TLS lookup nor another worker registration.
		worker register_worker() noexcept
		{
			return worker(shards[next_worker_slot()]);
		}

		// Columns: translated PPU/SPU words, interpreter PPU/SPU dispatches.
		void add(unsigned column, std::uint64_t count = 1) noexcept
		{
			shards[worker_slot()].values[column].fetch_add(count, std::memory_order_relaxed);
		}

		std::array<std::uint64_t, 4> snapshot() const noexcept
		{
			std::array<std::uint64_t, 4> result{};
			for (const auto& shard : shards)
				for (unsigned i = 0; i < result.size(); ++i)
					result[i] += shard.values[i].load(std::memory_order_relaxed);
			return result;
		}

		// Only while emulation is stopped, as with selecting the dispatch table.
		void reset() noexcept
		{
			for (auto& shard : shards)
				for (auto& value : shard.values)
					value.store(0, std::memory_order_relaxed);
		}
	};
} // namespace rpcs3::ios::aot
