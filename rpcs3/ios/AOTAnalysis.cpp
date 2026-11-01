#include "Emu/Cell/lv2/sys_sync.h"
#include "AOTAnalysis.h"
#include "AOTError.h"
#include "AOTRecompiler.h"
#include "AOTDispatch.h"
#include "Emu/Cell/PPUThread.h"
#include "Emu/Cell/PPUAnalyser.h"
#include "Emu/Cell/SPURecompiler.h"
#include "Loader/ELF.h"
#include "Utilities/File.h"
#include <algorithm>
#include <stdexcept>
#include <filesystem>
#include <bit>
#include <bitset>
#include <array>
#include <deque>
#include <mutex>
#include <cstring>

namespace rpcs3::ios::aot
{
	void analyze_spu_program(const be_t<u32>* local_store, u32 entry, instruction_inventory& inventory,
		const std::function<void()>& checkpoint);

	namespace
	{
		struct spu_snapshot
		{
			const void* owner;
			std::vector<be_t<u32>> words;
			std::bitset<65536> seeds;
			std::array<u64, 1024> observed{}; // Previously observed instruction words, excluding unrelated data.
		};
		std::mutex snapshots_mutex;
		std::deque<spu_snapshot> snapshots; // At most16MiB of LS plus1MiB seed/observed bits.
	} // namespace
	void observe_spu_program(const void* owner, const be_t<u32>* local_store, u32 entry, std::span<const u32> observed)
	{
		if (!owner || !local_store || observed.empty() || observed.size() > 32 || entry % 4 ||
			entry >= 0x40000 || observed.size() > (0x40000 - entry) / 4)
			return;
		std::lock_guard lock(snapshots_mutex);
		const auto matches = [&](const auto& snapshot)
		{
			return std::equal(observed.begin(), observed.end(), snapshot.words.begin() + entry / 4, [](u32 a, be_t<u32> b)
				{
					return a == +b;
				});
		};
		const auto remember = [&](spu_snapshot& snapshot)
		{
			snapshot.seeds.set(entry / 4);
			for (u32 i = 0; i < observed.size(); ++i)
			{
				const auto word = entry / 4 + i;
				snapshot.observed[word / 64] |= 1ull << (word % 64);
			}
		};
		spu_snapshot* previous = nullptr;
		for (auto it = snapshots.rbegin(); it != snapshots.rend(); ++it)
			if (it->owner == owner)
			{
				previous = &*it;
				if (matches(*it) && !std::memcmp(it->words.data(), local_store, 0x40000))
				{
					remember(*it);
					return;
				}
				break;
			}
		spu_snapshot snapshot{owner, std::vector<be_t<u32>>(65536), {}, {}};
		std::memcpy(snapshot.words.data(), local_store, 0x40000);
		if (!matches(snapshot))
			return; // DMA/code changed during capture; retain the short observation.
		if (previous)
		{
			bool same_code = true;
			for (usz group = 0; group < previous->observed.size() && same_code; ++group)
				for (auto bits = previous->observed[group]; bits; bits &= bits - 1)
				{
					const auto word = group * 64 + std::countr_zero(bits);
					if (previous->words[word] != snapshot.words[word])
					{
						same_code = false;
						break;
					}
				}
			if (same_code)
			{
				// Refresh unobserved bytes, retaining every known entry. Data-only
				// changes must not consume the bounded code-version history.
				previous->words.swap(snapshot.words);
				remember(*previous);
				return;
			}
		}
		remember(snapshot);
		if (snapshots.size() == 64)
			snapshots.pop_front();
		snapshots.push_back(std::move(snapshot));
	}
	void clear_runtime_spu_programs()
	{
		std::lock_guard lock(snapshots_mutex);
		snapshots.clear();
	}
	void collect_runtime_spu_programs(instruction_inventory& inventory)
	{
		std::deque<spu_snapshot> captured;
		{
			std::lock_guard lock(snapshots_mutex);
			captured.swap(snapshots);
		}
		std::size_t analyses = 0, words = 0;
		for (const auto& snapshot : captured)
		{
			std::bitset<65536> covered;
			for (u32 pc = 0; pc < 65536; ++pc)
			{
				if (!snapshot.seeds[pc] || covered[pc])
					continue;
				if (++analyses > 1024)
					return; // Bound Stop work; short observations remain available.
				instruction_inventory program;
				collect_spu_program(snapshot.words.data(), pc * 4, program);
				for (const auto& [unit, count] : program)
				{
					if (!inventory.contains(unit))
					{
						if (unit.words.size() > 4194304 - words)
							return;
						words += unit.words.size();
					}
					inventory[unit] += count;
					for (const auto& [offset, size] : unit.blocks)
						for (u32 i = 0; i < size && unit.address / 4 + offset + i < 65536; ++i)
							covered.set(unit.address / 4 + offset + i);
				}
			}
		}
	}

	void collect_ppu_functions(const ppu_module<lv2_obj>& module, instruction_inventory& inventory,
		const std::function<void()>& checkpoint)
	{
		for (const auto& function : module.get_funcs())
		{
			if (checkpoint)
				checkpoint();
			if (!function.size || function.size % 4 || function.size > 0x40000)
				continue;
			const auto words = module.get_ptr<u32>(function.addr, function.size);
			if (!words)
				continue;
			instruction unit{1, +words[0], {}};
			unit.address = function.addr;
			for (u32 i = 0; i < function.size / 4; ++i)
				unit.words.push_back(+words[i]);
			for (const auto& [addr, size] : function)
				if (addr >= function.addr && size && !(size % 4) && u64(addr) + size <= u64(function.addr) + function.size)
					unit.blocks.emplace_back((addr - function.addr) / 4, size / 4);
			if (!unit.blocks.empty())
				++inventory[std::move(unit)];
		}
	}

	void observe_ppu_functions(const ppu_module<lv2_obj>& module)
	{
		instruction_inventory inventory;
		collect_ppu_functions(module, inventory);
		std::vector<instruction> entries;
		entries.reserve(inventory.size());
		for (const auto& [entry, frequency] : inventory)
		{
			(void)frequency;
			entries.push_back(entry);
		}
		restore_discoveries(entries);
	}

	void collect_native_block(u32 kind, const be_t<u32>* words, u32 count, instruction_inventory& inventory)
	{
		while (count)
		{
			u32 size = 0;
			do
			{
				++size;
			} while (size < std::min<u32>(count, 32) && !terminates_unit(kind, +words[size - 1]) && known_instruction(kind, +words[size]));
			instruction unit{kind, +words[0], {}};
			for (u32 i = 0; i < size; ++i)
				unit.words.push_back(+words[i]);
			if (supported(unit))
			{
				if (inventory.size() >= 1048576 && !inventory.contains(unit))
					throw failure{"Native AOT block inventory exceeds its limit"};
				++inventory[std::move(unit)];
			}
			words += size;
			count -= size;
		}
	}

	namespace
	{
		using ppu_elf = elf_object<elf_be, u64, elf_machine::ppc64, elf_os::none, elf_type::none>;
		using spu_elf = elf_object<elf_be, u32, elf_machine::spu, elf_os::none, elf_type::none>;

		template <typename ELF>
		bool load_native_elf(ELF& elf, const fs::file& input)
		{
			// Use the native parser twice: bounded header validation precedes owned
			// segment allocation. No second ELF parser or relocation implementation.
			if (elf.open(input, 0, elf_opt::no_data) != elf_error::ok)
				return false;
			u64 memory = 0, file_memory = 0;
			if (elf.progs.size() > 4096)
				return false;
			for (const auto& segment : elf.progs)
			{
				const u64 offset = segment.p_offset, size = segment.p_filesz, memsz = segment.p_memsz;
				if (offset > input.size() || size > input.size() - offset || size > 512 * 1024 * 1024 - file_memory)
					return false;
				file_memory += size;
				if (segment.p_type != 1)
					continue;
				if constexpr (std::is_same_v<ELF, spu_elf>)
				{
					const u64 address = segment.p_vaddr;
					// Reject an invalid local-store range before the native ELF loader
					// allocates its segment data, not after a potentially large allocation.
					if (address > 0x40000 || memsz > 0x40000 - address)
						return false;
				}
				const u64 padded = utils::align<u64>(memsz, 0x10000);
				if (size > memsz || padded > 512 * 1024 * 1024 || memory > 512 * 1024 * 1024 - padded)
					return false;
				memory += padded;
			}
			// Retain only section headers; segment bytes already own their contents.
			auto sections = std::move(elf.shdrs);
			if (elf.open(input, 0, elf_opt::no_sections) != elf_error::ok)
				return false;
			elf.shdrs = std::move(sections);
			return true;
		}

		analysis_result analyze_image(std::span<const std::uint8_t> bytes, instruction_inventory& inventory,
			const std::function<void()>& checkpoint, bool embedded, bool include_ppu)
		{
			if (checkpoint)
				checkpoint();
			if (bytes.size() < 52 || bytes.size() > 512 * 1024 * 1024)
				return {};
			const auto input = fs::make_stream(std::vector<u8>(bytes.begin(), bytes.end()));
			spu_elf spu;
			if (load_native_elf(spu, input))
			{
				std::vector<be_t<u32>> local_store(0x40000 / 4);
				for (const auto& segment : spu.progs)
				{
					if (segment.p_type != 1)
						continue;
					const u64 addr = segment.p_vaddr, size = segment.p_memsz;
					if (addr > 0x40000 || size > 0x40000 - addr)
						return {};
					std::memcpy(reinterpret_cast<u8*>(local_store.data()) + addr, segment.bin.data(), segment.bin.size());
				}
				if (spu.header.e_entry >= 0x40000 || spu.header.e_entry % 4 ||
					!std::any_of(spu.progs.begin(), spu.progs.end(), [&](const auto& segment)
						{
							return segment.p_type == 1 && (segment.p_flags & 1) && spu.header.e_entry >= segment.p_vaddr &&
					               u64(spu.header.e_entry) + 4 <= u64(segment.p_vaddr) + segment.p_filesz;
						}))
					return {};
				analyze_spu_program(local_store.data(), spu.header.e_entry, inventory, checkpoint);
				return {0, 1};
			}
			if (embedded)
				return {};
			ppu_elf elf;
			if (!load_native_elf(elf, input))
				return {};
			if (include_ppu)
			{
				ppu_module<lv2_obj> module;
				module.name = "AOT file analysis";
				for (const auto& segment : elf.progs)
				{
					if (checkpoint)
						checkpoint();
					if (segment.p_type != 1 || !segment.p_memsz)
						continue;
					const u64 addr = segment.p_vaddr, size = segment.p_memsz;
					if (addr > UINT32_MAX || size > UINT32_MAX - addr)
						return {};
					const auto storage = std::shared_ptr<void>(std::calloc(1, utils::align<u64>(size, 0x10000)), std::free);
					if (!storage)
						throw failure{"Insufficient memory for native AOT analysis"};
					std::memcpy(storage.get(), segment.bin.data(), segment.bin.size());
					module.addr_to_seg_index.emplace(addr, module.segs.size());
					module.segs.push_back({u32(addr), u32(size), 1, +segment.p_flags, u32(segment.bin.size()), storage.get()});
					module.allocations.push_back(storage);
				}
				if (module.segs.empty())
					return {};
				// The native PPU analyser treats segment zero as code. Preserve that
				// contract and never feed data-only bytes to its instruction scanner.
				const auto executable = std::find_if(module.segs.begin(), module.segs.end(), [](const auto& segment)
					{
						return segment.flags & 1;
					});
				if (executable == module.segs.end())
					return {};
				if (executable != module.segs.begin())
				{
					std::iter_swap(module.segs.begin(), executable);
					module.addr_to_seg_index.clear();
					for (usz i = 0; i < module.segs.size(); ++i)
						module.addr_to_seg_index.emplace(module.segs[i].addr, i);
				}
				u32 code_end = 0;
				for (const auto& section : elf.shdrs)
					code_end = std::max(code_end, module.add_analysis_section(static_cast<u32>(static_cast<sec_type>(section.sh_type)),
													  section.sh_addr, section.sh_size, section._sh_flags));
				if (module.segs[0].addr && elf.header.e_entry <= UINT32_MAX &&
					module.analyse(0, u32(+elf.header.e_entry), code_end, {}, {}, [&]
						{
							if (checkpoint)
								checkpoint();
							return false;
						}))
				{
					collect_ppu_functions(module, inventory, checkpoint);
				}
				// PRX relocation remains native-loader-owned. Unrelocated file words may be
				// prepared conservatively; runtime byte matching rejects changed code.
				else
					for (const auto& segment : module.segs)
						if (segment.flags & 1)
							collect_native_block(1, static_cast<const be_t<u32>*>(segment.ptr), segment.filesz / 4, inventory);
			}
			analysis_result result{1, 0};
			for (const auto& segment : elf.progs)
			{
				if (segment.p_type != 1)
					continue;
				ppu_find_spu_code(segment.bin, [&](u32 address, std::span<const u8> data)
					{
						analyze_spu_segment(address, data, inventory, checkpoint);
						++result.spu_images;
					},
					[&](u32 offset) -> u32
					{
						// Keep the bounded native ELF loader above as the validation owner.
					    // Do not trust scanner matches to allocate arbitrary segment lengths.
						result.spu_images += analyze_image(std::span(segment.bin).subspan(offset), inventory, checkpoint, true, false).spu_images;
						return 0;
					},
					checkpoint);
			}
			return result;
		}
	} // namespace
	void analyze_spu_segment(u32 address, std::span<const u8> bytes, instruction_inventory& inventory,
		const std::function<void()>& checkpoint)
	{
		if (address % 4 || address >= 0x40000 || bytes.size() > 0x40000 - address)
			return;
		std::vector<be_t<u32>> local_store(0x40000 / 4);
		std::memcpy(reinterpret_cast<u8*>(local_store.data()) + address, bytes.data(), bytes.size());
		analyze_spu_program(local_store.data(), UINT32_MAX, inventory, checkpoint);
	}
	void collect_cached_spu_programs(const std::string& directory, instruction_inventory& inventory, const std::function<void()>& checkpoint)
	{
		namespace path = std::filesystem;
		if (!path::is_directory(path::symlink_status(directory)))
			return;
		std::size_t visited = 0;
		for (auto it = path::recursive_directory_iterator(directory); it != path::recursive_directory_iterator(); ++it)
		{
			if (checkpoint)
				checkpoint();
			if (++visited > 1000000)
				throw failure{"Native cache directory exceeds the AOT discovery limit"};
			const auto name = it->path().filename().string();
			if (it->is_symlink() || (it->is_directory() && name == "shaders_cache"))
			{
				it.disable_recursion_pending();
				continue;
			}
			if (!it->is_regular_file() || !name.starts_with("spu-") || !name.ends_with("-v1-tane.dat") || it->file_size() > 128 * 1024 * 1024)
				continue;
			// Read the existing native cache format and CRC policy, without opening
			// it for append or invoking SPU runtime/JIT initialization.
			spu_cache cache{fs::file(it->path().string(), fs::read)};
			cache.for_each([&](spu_program&& program)
				{
					if (checkpoint)
						checkpoint();
					std::vector<be_t<u32>> local_store(0x40000 / 4);
					for (usz i = 0; i < program.data.size(); ++i)
						local_store[program.lower_bound / 4 + i] = std::bit_cast<be_t<u32>>(program.data[i]);
					collect_spu_program(local_store.data(), program.entry_point, inventory);
					if (inventory.size() > 1048576)
						throw failure{"Native SPU observations exceed the AOT inventory limit"};
					return true;
				});
		}
	}

	analysis_result analyze_elf(std::span<const std::uint8_t> bytes, instruction_inventory& inventory, const std::function<void()>& checkpoint, bool include_ppu)
	{
		return analyze_image(bytes, inventory, checkpoint, false, include_ppu);
	}
} // namespace rpcs3::ios::aot
