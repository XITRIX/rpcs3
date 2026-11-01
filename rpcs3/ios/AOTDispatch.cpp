#include "AOTDispatch.h"
#ifdef RPCS3_IOS_AOT_BACKEND
#include "AOTAnalysis.h"
#include "AOTNativeGateway.h"
#include "AOTCodeMatch.h"
#include "Emu/Cell/SPUThread.h"
#endif
#include "AOTCounters.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <set>
#include <optional>
#include "Emu/Cell/PPUAnalyser.h"
#include "Emu/Cell/SPUAnalyser.h"
#include "Emu/Cell/SPUOpcodes.h"

extern const ppu_decoder<ppu_itype> g_ppu_itype;
extern const spu_decoder<spu_itype> g_spu_itype;

namespace rpcs3::ios::aot
{
	namespace
	{
		std::uint64_t append(std::uint64_t hash, std::uint32_t word)
		{
			return (hash ^ word) * 1099511628211ull;
		}
		std::uint64_t entry_key(std::uint32_t kind, std::uint32_t opcode)
		{
			return (std::uint64_t(kind) << 32) | opcode;
		}
		// Enable only for diagnostic runs; normal performance windows keep this off.
		const bool profile_enabled = []
		{
			const auto* value = std::getenv("RPCS3_AOT_PROFILE");
			return value && std::string_view(value) == "1";
		}();
		runtime_counters profile_counts;
		bool checked_matches(const rpcs3_aot_runtime_context_v2& context, const std::uint32_t* words, std::uint32_t count)
		{
			if (profile_enabled) [[unlikely]]
			{
				thread_local const auto worker = profile_counts.register_worker();
				worker.add(2);
				worker.add(3, count);
			}
			return context.matches_code(&context, words, count);
		}
		bool matches(const rpcs3_aot_entry_v2& entry, const rpcs3_aot_runtime_context_v2& context)
		{
			if (!entry.entry_offset && !entry.verification_flags)
				return checked_matches(context, entry.words, entry.word_count);
			if (entry.entry_offset > context.guest_pc / 4)
				return false;
			auto base = context;
			base.verification_flags = entry.verification_flags;
			base.guest_pc -= entry.entry_offset * 4;
			return checked_matches(base, entry.words, entry.word_count);
		}
		// A single mismatching guest word proves that a prepared program cannot
		// match. Retain that witness rather than rescanning a large candidate on each
		// interpreted opcode. Restoring the expected word invalidates the proof.
		struct mismatch
		{
			std::int32_t offset;
			std::uint32_t expected;
			auto operator<=>(const mismatch&) const = default;
		};
		bool still_mismatches(const mismatch& word, const rpcs3_aot_runtime_context_v2& context)
		{
			const auto address = std::int64_t(context.guest_pc) + std::int64_t(word.offset) * 4;
			if (address < 0 || address > UINT32_MAX)
				return true;
			auto at = context;
			at.guest_pc = address;
			return !checked_matches(at, &word.expected, 1);
		}
		std::optional<mismatch> mismatch_in(const rpcs3_aot_entry_v2& entry, const rpcs3_aot_runtime_context_v2& context)
		{
			auto at = context;
			at.verification_flags = entry.verification_flags;
			at.guest_pc -= entry.entry_offset * 4;
			std::uint32_t first = 0, count = entry.word_count;
			while (count > 1)
			{
				const auto half = count / 2;
				if (checked_matches(at, entry.words + first, half))
				{
					first += half;
					at.guest_pc += half * 4;
					count -= half;
				}
				else
					count = half;
			}
			mismatch word{std::int32_t(first) - std::int32_t(entry.entry_offset), entry.words[first]};
			return still_mismatches(word, context) ? std::optional(word) : std::nullopt;
		}
		std::atomic<std::uint64_t> live_tables{0};
		struct table
		{
			table()
			{
				live_tables.fetch_add(1, std::memory_order_relaxed);
			}
			~table()
			{
				live_tables.fetch_sub(1, std::memory_order_relaxed);
			}
			std::uint64_t serial = 0;
			bool ppu_by_address = false;
			std::deque<rpcs3_aot_entry_v2> ppu_entries;
			std::unordered_map<std::uint64_t, std::vector<rpcs3_aot_entry_v2>> entries;
			std::unordered_map<std::uint64_t, std::vector<const rpcs3_aot_entry_v2*>> addresses;
		};
		std::unique_ptr<table> owned_table;
		std::atomic<const table*> current_table = nullptr;
		std::atomic<std::uint64_t> compiled_ppu, compiled_spu, pending;
		runtime_counters counters;
		std::atomic<std::uint64_t> epoch;
		std::mutex discovery_mutex;
		std::set<instruction> discovered;
	} // namespace

#ifdef RPCS3_IOS_AOT_BACKEND
	namespace
	{
		struct native_spu_table
		{
			std::unordered_map<u32, std::vector<native_spu_selection>> entries;
			void* find(spu_thread& cpu, const void* ls) const
			{
				if (cpu.pc >= 0x40000 || cpu.pc % 4)
					return nullptr;
				const u32 opcode = static_cast<const be_t<u32>*>(ls)[cpu.pc / 4];
				const auto found = entries.find(opcode);
				if (found == entries.end())
					return nullptr;
				for (const auto& candidate : found->second)
				{
					const auto& identity = candidate.identity;
					if (identity.entry_offset > cpu.pc / 4)
						continue;
					const u32 base = cpu.pc - identity.entry_offset * 4;
					if (identity.word_count > (0x40000 - base) / 4)
						continue;
					if (matches_guest_words(static_cast<const u8*>(ls) + base, identity.words, identity.word_count,
							identity.verification_flags & RPCS3_AOT_SPARSE_SPU))
						return candidate.target;
				}
				return nullptr;
			}
		};
		std::unique_ptr<native_spu_table> native_spu_owned;
		std::atomic<const native_spu_table*> native_spu_current = nullptr;
		runtime_counters native_counts;
	} // namespace
	bool select_native_spu_entries(std::span<const native_spu_selection> entries)
	{
		auto next = std::make_unique<native_spu_table>();
		for (const auto& item : entries)
		{
			const auto& identity = item.identity;
			if (!item.target || identity.guest_kind != 2 || !identity.words || !identity.word_count ||
				identity.word_count > 65536 || identity.entry_offset >= identity.word_count)
				return false;
			next->entries[identity.opcode].push_back(item);
		}
		for (auto& [opcode, candidates] : next->entries)
			std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b)
				{
					return a.identity.word_count > b.identity.word_count;
				});
		native_spu_current.store(next.get(), std::memory_order_release);
		native_spu_owned = std::move(next);
		native_counts.reset();
		compiled_spu.fetch_add(entries.size());
		return true;
	}
	bool execute_native_spu(spu_thread& cpu, void* ls)
	{
		const auto* selected = native_spu_current.load(std::memory_order_acquire);
		if (!selected || selected->entries.empty())
			return false;
		auto* target = selected->find(cpu, ls);
		if (!target)
			return false;
		const native_spu_dispatch dispatch{const_cast<native_spu_table*>(selected), [](void* owner, void* cpu, void* ls)
			{
				return static_cast<native_spu_table*>(owner)->find(*static_cast<spu_thread*>(cpu), ls);
			}};
		const std::array<u64, 7> arguments{reinterpret_cast<u64>(&cpu), reinterpret_cast<u64>(ls), 0, 0, 0, 0, 0};
		// Reuse the existing bounded per-worker counters: native SPU dispatch
		// must not make every emulated SPU contend for a single telemetry line.
		thread_local const auto counts = native_counts.register_worker();
		counts.add(0);
		invoke_native(target, arguments, &cpu, cpu.hv_ctx, &dispatch);
		return true;
	}
#else
	bool select_native_spu_entries(std::span<const native_spu_selection> entries)
	{
		return entries.empty();
	}
	bool execute_native_spu(spu_thread&, void*)
	{
		return false;
	}
#endif

	bool known_instruction(std::uint32_t kind, std::uint32_t opcode)
	{
		return kind == 1 ? g_ppu_itype.decode(opcode) != ppu_itype::UNK :
		                   kind == 2 && g_spu_itype.decode(opcode) != spu_itype::UNK;
	}

	bool terminates_unit(std::uint32_t kind, std::uint32_t opcode)
	{
		if (kind == 1)
		{
			const auto type = g_ppu_itype.decode(opcode);
			return type == ppu_itype::UNK || type & ppu_itype::branch || type & ppu_itype::trap || type == ppu_itype::SC;
		}
		const auto type = g_spu_itype.decode(opcode);
		return type == spu_itype::UNK || type & spu_itype::branch || type == spu_itype::STOP || type == spu_itype::STOPD ||
		       type == spu_itype::RDCH || type == spu_itype::WRCH || type == spu_itype::RCHCNT;
	}

	void select_entries(std::span<const rpcs3_aot_entry_v2> entries, std::span<const std::uint32_t> origins)
	{
#ifdef RPCS3_IOS_AOT_BACKEND
		native_spu_current.store(nullptr, std::memory_order_release);
		native_spu_owned.reset();
		native_counts.reset();
#endif
		auto selected = std::make_unique<table>();
		selected->serial = epoch.fetch_add(1, std::memory_order_acq_rel) + 1;
		selected->ppu_by_address = !origins.empty() && origins.size() == entries.size();
		const auto add_entry = [&](const rpcs3_aot_entry_v2& entry, std::uint32_t origin)
		{
			if (selected->ppu_by_address && entry.guest_kind == 1)
			{
				if (origin == UINT32_MAX || std::uint64_t(origin) + entry.entry_offset * 4 > UINT32_MAX)
					return;
				// LLVM may merge identical word arrays belonging to different guest
				// functions. Preserve each descriptor's own origin, not one per array.
				selected->ppu_entries.push_back(entry);
				selected->addresses[entry_key(1, origin + entry.entry_offset * 4)].push_back(&selected->ppu_entries.back());
			}
			else
				selected->entries[entry_key(entry.guest_kind, entry.opcode)].push_back(entry);
		};
		for (std::size_t index = 0; index < entries.size(); ++index)
		{
			const auto& entry = entries[index];
			const auto origin = selected->ppu_by_address ? origins[index] : UINT32_MAX;
			if (entry.invoke && entry.words && entry.word_count && entry.word_count <= 65536 && entry.entry_offset < entry.word_count)
			{
				add_entry(entry, origin);
				if (entry.alternate_count <= entry.word_count && entry.alternate_offsets)
					for (std::uint32_t i = 0; i < entry.alternate_count; ++i)
						if (const auto offset = entry.alternate_offsets[i]; offset < entry.word_count && offset != entry.entry_offset)
						{
							auto resumed = entry;
							resumed.opcode = entry.words[offset];
							resumed.entry_offset = offset;
							add_entry(resumed, origin);
						}
			}
		}
		for (auto& [key, candidates] : selected->entries)
			std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b)
				{
					return a.word_count > b.word_count;
				});
		// Game PPU dispatch is address based. A missing or relocated entry falls
		// back to the native interpreter; it must never search unrelated functions
		// sharing an opcode. Loader observations own subsequent preparation.
		if (origins.size() == entries.size())
		{
			std::unordered_map<const std::uint32_t*, std::uint32_t> code_origins;
			for (std::size_t i = 0; i < entries.size(); ++i)
				if (entries[i].guest_kind != 1 && origins[i] != UINT32_MAX)
					code_origins.emplace(entries[i].words, origins[i]);
			for (const auto& [key, candidates] : selected->entries)
				for (const auto& entry : candidates)
					if (const auto at = code_origins.find(entry.words); at != code_origins.end() &&
																		std::uint64_t(at->second) + entry.entry_offset * 4 <= UINT32_MAX)
						selected->addresses[entry_key(entry.guest_kind, at->second + entry.entry_offset * 4)].push_back(&entry);
		}
		for (auto& [address, candidates] : selected->addresses)
			std::stable_sort(candidates.begin(), candidates.end(), [](const auto* a, const auto* b)
				{
					return a->word_count > b->word_count;
				});
		compiled_ppu = std::count_if(entries.begin(), entries.end(), [](const auto& entry)
			{
				return entry.guest_kind == 1;
			});
		compiled_spu = entries.size() - compiled_ppu.load();
		counters.reset();
		profile_counts.reset();
		pending = 0;
		{
			std::lock_guard lock(discovery_mutex);
			discovered.clear();
		}
		current_table.store(selected.get(), std::memory_order_release);
		owned_table = std::move(selected); // CPU workers are quiescent; executable libraries remain loaded.
	}

	bool execute(std::uint32_t guest_kind, std::uint32_t opcode, rpcs3_aot_runtime_context_v2& context)
	{
		const auto* selected = current_table.load(std::memory_order_acquire);
		const bool valid_context = context.structure_size >= sizeof(context) && context.version == RPCS3_AOT_RUNTIME_VERSION;
		if (guest_kind == 1 && selected && selected->ppu_by_address)
		{
			// No candidate-search cache, mismatch proofs, or short-code discovery
			// on the production PPU path. Cost depends only on this guest address.
			// Whole-function discovery already happens in the native loader.
			thread_local const auto counts = counters.register_worker();
			if (valid_context && context.matches_code)
				if (const auto at = selected->addresses.find(entry_key(1, context.guest_pc)); at != selected->addresses.end())
					for (const auto* entry : at->second)
						if (entry->opcode == opcode && matches(*entry, context))
						{
							context.remaining_words = 4096;
							context.executed_words = 0;
							context.code_base_pc = context.guest_pc - entry->entry_offset * 4;
							entry->invoke(&context);
							counts.add(0, context.executed_words);
							if (profile_enabled) [[unlikely]]
							{
								thread_local const auto profile = profile_counts.register_worker();
								profile.add(0);
							}
							return true;
						}
			counts.add(2);
			return false;
		}

		// Keep the cheap positive fast path for small hot loops. The paged second
		// level below retains both hits and misses across larger working sets.
		struct cached_entry;
		struct hot_entry
		{
			const table* generation = nullptr;
			std::uint64_t serial = 0;
			const void* cpu = nullptr;
			std::uint32_t pc = 0;
			std::uint32_t kind = 0;
			const rpcs3_aot_entry_v2* entry = nullptr;
			cached_entry* cached = nullptr;
		};
		struct cached_entry
		{
			const rpcs3_aot_entry_v2* entry = nullptr;
			std::uint32_t missed_words = 0;
			std::uint16_t reselect_after = 0;
			bool has_larger = false;
			std::uint32_t proof_start = 0, proof_count = 0;
			std::uint64_t proof_epoch = 0;
			std::array<std::uint32_t, 32> words{};
		};
		struct cached_page
		{
			const table* generation = nullptr;
			std::uint64_t serial = 0;
			const void* cpu = nullptr;
			std::uint32_t pc_page = 0;
			std::uint32_t kind = 0;
			std::array<cached_entry, 256> entries{};
		};
		struct thread_cache
		{
			std::array<hot_entry, 256> hot{};
			std::array<std::unique_ptr<cached_page>, 256> pages{};
			std::array<std::uint64_t, 4096> seen{};
			std::uint64_t seen_epoch = 0;
			std::vector<mismatch> proofs; // bounded to512KiB per worker
			std::uint64_t proof_epoch = 1;
			runtime_counters::worker counts = counters.register_worker();
			runtime_counters::worker profile = profile_counts.register_worker();
		};
		thread_local thread_cache storage;
		auto& cache = storage; // Resolve the TLS object once for this invocation.
		auto& hot_slot = cache.hot[valid_context ? ((context.guest_pc >> 2) ^ (context.guest_pc >> 10) ^
													   (reinterpret_cast<std::uintptr_t>(context.cpu) >> 6)) %
													   cache.hot.size() :
												   0];
		cached_entry* cached_slot = nullptr;
		const auto invoke = [&](const rpcs3_aot_entry_v2* entry)
		{
			if (cached_slot)
			{
				if (cached_slot->entry != entry)
				{
					const auto& candidates = selected->entries.at(entry_key(guest_kind, opcode));
					cached_slot->has_larger = entry->word_count <= 32 && candidates.front().word_count > entry->word_count;
				}
				if (cached_slot->has_larger && !cached_slot->reselect_after)
					cached_slot->reselect_after = 4096;
				cached_slot->entry = entry;
				cached_slot->missed_words = 0;
			}
			hot_slot = {selected, selected->serial, context.cpu, context.guest_pc, guest_kind, entry,
				cached_slot ? cached_slot : hot_slot.cached};
			context.remaining_words = 4096;
			context.executed_words = 0;
			context.code_base_pc = context.guest_pc - entry->entry_offset * 4;
			if (profile_enabled) [[unlikely]]
				cache.profile.add(guest_kind == 1 ? 0 : 1);
			entry->invoke(&context);
			cache.counts.add(guest_kind == 1 ? 0 : 1, context.executed_words);
			return true;
		};
		// A valid short prefix can outlive the surrounding SPU program after DMA.
		// Periodically reconsider larger prepared units at this PC. Every invocation
		// still verifies its complete identity; this only prevents a cached prefix
		// from permanently hiding a now-matching native program. A 4096-hit bound
		// avoids re-running common-opcode searches after every short hot loop.
		bool reconsider = false;
		if (selected && valid_context && context.matches_code && hot_slot.generation == selected &&
			hot_slot.serial == selected->serial && hot_slot.cpu == context.cpu && hot_slot.pc == context.guest_pc && hot_slot.kind == guest_kind)
		{
			if (const auto* entry = hot_slot.entry; entry && entry->opcode == opcode &&
													matches(*entry, context))
			{
				if (auto* cached = hot_slot.cached; cached && cached->entry == entry &&
													cached->reselect_after && !--cached->reselect_after)
					reconsider = true;
				else
					return invoke(entry);
			}
			hot_slot.entry = nullptr;
		}
		// A small instruction direct map thrashes across ordinary guest loops.
		// Allocate cache pages on demand, with one slot for each PC in a 1KiB guest
		// page. The directory covers the complete 256KiB SPU local-store address
		// space without aliasing; higher PPU page bits are folded into its index.
		// Each worker owns a bounded directory (under 11MiB including mismatch proofs when completely populated).
		// Every hit still validates all guest bytes. Pages remain allocated until
		// worker exit, so native helper re-entry cannot free an outer cache slot.
		static_assert(sizeof(cached_page) * 256 + 65536 * sizeof(mismatch) < 11 * 1024 * 1024);
		if (selected && valid_context && context.matches_code && !(context.guest_pc & 3))
		{
			const auto pc_page = context.guest_pc >> 10;
			auto& page = cache.pages[(pc_page ^ (pc_page >> 8) ^ (pc_page >> 16)) % cache.pages.size()];
			if (!page)
				page = std::make_unique<cached_page>();
			if (page->generation != selected || page->serial != selected->serial || page->cpu != context.cpu ||
				page->pc_page != pc_page || page->kind != guest_kind)
			{
				page->entries.fill({});
				page->generation = selected;
				page->serial = selected->serial;
				page->cpu = context.cpu;
				page->pc_page = pc_page;
				page->kind = guest_kind;
			}
			cached_slot = &page->entries[(context.guest_pc >> 2) % page->entries.size()];
		}
		if (cached_slot)
		{
			auto& cached = *cached_slot;
			if (const auto* entry = cached.entry; !reconsider && entry && entry->opcode == opcode &&
												  matches(*entry, context))
			{
				if (cached.reselect_after && !--cached.reselect_after)
					reconsider = true;
				else
					return invoke(entry);
			}
			if (!cached.entry && cached.missed_words && cached.words[0] == opcode &&
				checked_matches(context, cached.words.data(), cached.missed_words) &&
				cached.proof_epoch == cache.proof_epoch &&
				std::all_of(cache.proofs.begin() + cached.proof_start, cache.proofs.begin() + cached.proof_start + cached.proof_count,
					[&](const auto& word)
					{
						return still_mismatches(word, context);
					}))
			{
				// A previous miss already recorded this exact observed code window.
				// Avoid rediscovering and searching native candidates before each
				// interpreted instruction. Changed bytes still take the discovery path.
				cache.counts.add(guest_kind == 1 ? 2 : 3);
				return false;
			}
		}
		bool cacheable_miss = cached_slot && valid_context && context.matches_code;
		std::vector<mismatch> proofs;
		if (selected && valid_context && context.matches_code)
		{
			if (const auto at = selected->addresses.find(entry_key(guest_kind, context.guest_pc)); at != selected->addresses.end())
				for (const auto* entry : at->second)
					if (entry->opcode == opcode && matches(*entry, context))
					{
						return invoke(entry);
					}
			// Native analysis owns program boundaries. Select by the entry opcode,
			// then verify the complete function/program, including code before entry.
			if (const auto found = selected->entries.find(entry_key(guest_kind, opcode)); found != selected->entries.end())
				for (const auto& entry : found->second)
				{
					if (matches(entry, context))
					{
						return invoke(&entry);
					}
					if (cacheable_miss && entry.entry_offset <= context.guest_pc / 4)
					{
						if (const auto word = mismatch_in(entry, context))
							proofs.push_back(*word);
						else
							cacheable_miss = false;
						if (proofs.size() >= 4096)
						{
							std::sort(proofs.begin(), proofs.end());
							proofs.erase(std::unique(proofs.begin(), proofs.end()), proofs.end());
							if (proofs.size() >= 4096)
								cacheable_miss = false;
						}
					}
				}
		}
		std::array<std::uint32_t, 32> words{};
		std::uint32_t count = 0;
		if (valid_context && context.read_code)
			count = std::min<std::uint32_t>(context.read_code(&context, words.data(), words.size()), words.size());
		const auto observed_count = count;
		cacheable_miss = cacheable_miss && count && words[0] == opcode;
		if (!count || words[0] != opcode)
		{
			words[0] = opcode;
			count = 1;
		}
		auto hash = append(14695981039346656037ull, guest_kind);
		for (std::uint32_t i = 0; i < count; ++i)
		{
			if (!known_instruction(guest_kind, words[i]) && i)
			{
				count = i;
				break;
			}
			hash = append(hash, words[i]);
			if (terminates_unit(guest_kind, words[i]))
			{
				count = i + 1;
				break;
			}
		}
		const auto identity = hash;
		cache.counts.add(guest_kind == 1 ? 2 : 3);
		// Per-thread duplicate filter avoids a global lock on every interpreted
		// dispatch. A bounded collision replaces only the filter, never guest state.
		const auto generation = epoch.load(std::memory_order_acquire);
		if (cache.seen_epoch != generation)
		{
			cache.seen.fill(0);
			cache.seen_epoch = generation;
		}
		auto& slot = cache.seen[(identity ^ (identity >> 17)) % cache.seen.size()];
		if (slot != identity)
		{
			slot = identity;
			{
				std::lock_guard lock(discovery_mutex);
				// Bound short discovery independently of the number of CPU threads.
				if (discovered.size() < 65536 && discovered.insert({guest_kind, words[0], {words.begin(), words.begin() + count}}).second)
					pending.fetch_add(1, std::memory_order_relaxed);
			}
#ifdef RPCS3_IOS_AOT_BACKEND
			// Native PPU function observations can fill that queue before the first
			// SPU miss. The independently bounded snapshot owner must still run.
			if (guest_kind == 2 && valid_context)
				observe_spu_program(context.cpu, static_cast<const be_t<u32>*>(context.local_store), context.guest_pc,
					std::span(words).first(count));
#endif
		}
		if (cacheable_miss)
		{
			std::sort(proofs.begin(), proofs.end());
			proofs.erase(std::unique(proofs.begin(), proofs.end()), proofs.end());
			if (cache.proofs.size() + proofs.size() > 65536)
			{
				cache.proofs.clear();
				++cache.proof_epoch;
			}
			auto& cached = *cached_slot;
			cached.entry = nullptr;
			cached.proof_start = cache.proofs.size();
			cached.proof_count = proofs.size();
			cached.proof_epoch = cache.proof_epoch;
			if (!proofs.empty() && cache.proofs.capacity() < 65536)
				cache.proofs.reserve(65536);
			cache.proofs.insert(cache.proofs.end(), proofs.begin(), proofs.end());
			// Include the complete read window, even a terminal unknown opcode
			// excluded from discovery. Replacing that boundary must invalidate a miss.
			cached.missed_words = observed_count;
			std::copy_n(words.begin(), observed_count, cached.words.begin());
		}
		else if (cached_slot)
		{
			cached_slot->entry = nullptr;
			cached_slot->missed_words = 0;
		}
		return false;
	}

	bool has_prepared_address(std::uint32_t guest_kind, std::uint32_t address)
	{
		const auto* selected = current_table.load(std::memory_order_acquire);
		return selected && selected->addresses.contains(entry_key(guest_kind, address));
	}

	std::vector<std::uint32_t> prepared_addresses(std::uint32_t guest_kind, std::uint32_t start, std::uint32_t size)
	{
		std::vector<std::uint32_t> result;
		const auto* selected = current_table.load(std::memory_order_acquire);
		if (!selected)
			return result;
		const auto end = std::uint64_t(start) + size;
		for (const auto& [key, entries] : selected->addresses)
			if (key >> 32 == guest_kind && std::uint32_t(key) >= start && std::uint32_t(key) < end)
				result.push_back(std::uint32_t(key));
		std::sort(result.begin(), result.end());
		return result;
	}

	execution_statistics statistics() noexcept
	{
		const auto counts = counters.snapshot();
		const auto diagnostic = profile_counts.snapshot();
		return {compiled_ppu.load(), compiled_spu.load(), counts[0], counts[1], counts[2], counts[3], pending.load(),
			diagnostic[0], diagnostic[1], diagnostic[2], diagnostic[3], live_tables.load(std::memory_order_relaxed)
#ifdef RPCS3_IOS_AOT_BACKEND
																			,
			native_counts.snapshot()[0]
#endif
		};
	}

	std::vector<instruction> discoveries()
	{
		std::lock_guard lock(discovery_mutex);
		std::vector<instruction> result;
		result.reserve(discovered.size());
		result.assign(discovered.begin(), discovered.end());
		return result;
	}

	void restore_discoveries(std::span<const instruction> entries)
	{
		std::lock_guard lock(discovery_mutex);
		const auto* selected = current_table.load(std::memory_order_acquire);
		for (const auto& entry : entries)
		{
			const auto words = entry.words.empty() ? std::span<const u32>(&entry.opcode, 1) : std::span<const u32>(entry.words);
			bool prepared = false;
			if (selected)
				if (const auto found = selected->entries.find(entry_key(entry.guest_kind, entry.opcode)); found != selected->entries.end())
					prepared = std::any_of(found->second.begin(), found->second.end(), [&](const auto& candidate)
						{
							return candidate.guest_kind == entry.guest_kind && candidate.word_count == words.size() && candidate.entry_offset == entry.entry_offset &&
						           std::equal(words.begin(), words.end(), candidate.words);
						});
			// Already translated observations are not pending work in this session.
			if (!prepared && discovered.size() < 65536)
				discovered.insert(entry);
		}
		pending = discovered.size();
	}
} // namespace rpcs3::ios::aot
