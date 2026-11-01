#include "AOTNativeRuntime.h"
#include "AOTNativeImports.h"
#include "AOTNativeGateway.h"
#include "AOTNativeModule.h"
#include "Emu/Memory/vm.h"
#include "util/vm.hpp"
#include <cstring>
#include <dlfcn.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <algorithm>
#include <limits>
#include <map>
#include <cstdio>
#include <stdexcept>
#include <unordered_map>
extern const std::unordered_map<std::string, u64>& ppu_native_runtime_symbols();
namespace vm
{
	extern std::array<memory_page, 0x100000000 / 4096> g_pages;
}

namespace rpcs3::ios::aot
{
	const void* native_ppu_page_registry()
	{
		return vm::g_pages.data();
	}
	namespace
	{
		constexpr u64 ppu_table_size = vm::g_exec_addr_seg_offset + 0x80000000ull;
		bool ppu_table_range(u32 address, u32 size)
		{
			return size && !(address % 4) && !(size % 4) && u64(address) + size <= 0x100000000ull;
		}
	} // namespace

	native_ppu_table::~native_ppu_table()
	{
		if (m_storage)
			utils::memory_release(m_storage, ppu_table_size);
	}

	bool native_ppu_table::register_range(u32 address, u32 size)
	{
		if (!ppu_table_range(address, size))
			return false;
		if (!m_storage)
		{
			m_storage = static_cast<u8*>(utils::memory_reserve(ppu_table_size));
			if (!m_storage)
				return false;
		}
		const u64 end = (u64(address) + size + 0xffff) & ~0xffffull;
		for (u64 page = address & ~0xffffu; page < end; page += 0x10000)
		{
			if (m_pages[page >> 16])
				continue;
			utils::memory_commit(m_storage + page * 2, 0x20000, utils::protection::rw);
			utils::memory_commit(m_storage + vm::g_exec_addr_seg_offset + page / 2, 0x8000, utils::protection::rw);
			auto targets = reinterpret_cast<void**>(m_storage + page * 2);
			std::fill_n(targets, 0x4000, reinterpret_cast<void*>(&rpcs3_aot_native_ppu_fallback));
			std::memset(m_storage + vm::g_exec_addr_seg_offset + page / 2, 0, 0x8000);
			m_pages.set(page >> 16);
		}
		return true;
	}

	void* native_ppu_table::target(u32 address) const
	{
		if (address % 4 || !m_pages[address >> 16])
			return nullptr;
		return *reinterpret_cast<void**>(m_storage + u64(address) * 2);
	}

	bool native_ppu_table::bind(u32 address, u32 segment, void* entry)
	{
		if (!entry || address % 4 || segment % 8192 || segment >> 29 || !m_pages[address >> 16])
			return false;
		*reinterpret_cast<void**>(m_storage + u64(address) * 2) = entry;
		*reinterpret_cast<u16*>(m_storage + vm::g_exec_addr_seg_offset + address / 2) = u16(segment >> 13);
		return true;
	}

	bool native_ppu_table::invalidate(u32 address, u32 size)
	{
		if (!ppu_table_range(address, size))
			return false;
		const u64 end = u64(address) + size;
		for (u64 page = address & ~0xffffu; page < end; page += 0x10000)
		{
			if (!m_pages[page >> 16])
				continue;
			const u64 first = std::max<u64>(address, page);
			const u64 count = (std::min(end, page + 0x10000) - first) / 4;
			std::fill_n(reinterpret_cast<void**>(m_storage + first * 2), count,
				reinterpret_cast<void*>(&rpcs3_aot_native_ppu_fallback));
			std::memset(m_storage + vm::g_exec_addr_seg_offset + first / 2, 0, count * 2);
		}
		return true;
	}

	void native_ppu_table::reset()
	{
		if (m_storage)
			utils::memory_decommit(m_storage, ppu_table_size);
		m_pages.reset();
	}

	namespace
	{
		auto& imports()
		{
			static std::map<std::string_view, std::uintptr_t, std::less<>> values;
			return values;
		}
	} // namespace

	bool register_native_import(std::string_view key, std::uintptr_t address)
	{
		const auto [it, inserted] = imports().emplace(key, address);
		if (!address || (!inserted && it->second != address))
		{
			std::fprintf(stderr, "Conflicting native AOT helper registration: %.*s\n", int(key.size()), key.data());
			throw std::logic_error("Conflicting native AOT helper registration");
		}
		return true;
	}

	namespace
	{
		void native_spu_escape(void* cpu)
		{
			if (!escape_native_if_active(cpu))
				throw std::logic_error("SPU escape outside native gateway");
		}
	} // namespace
	std::uintptr_t resolve_native_import(std::string_view key)
	{
		if (key == "spu_escape")
			return reinterpret_cast<std::uintptr_t>(&native_import<&native_spu_escape>::call);
		const auto& ppu = ppu_native_runtime_symbols();
		if (const auto item = ppu.find(std::string(key)); item != ppu.end())
			return item->second;
		if (key == "__aot_ppu_fallback")
			return reinterpret_cast<std::uintptr_t>(&rpcs3_aot_native_ppu_fallback);
		if (key == "spu_dispatcher")
			return reinterpret_cast<std::uintptr_t>(&rpcs3_aot_native_spu_dispatch);
		if (key == "spu_dispatch")
			return reinterpret_cast<std::uintptr_t>(&rpcs3_aot_native_spu_reject);
		const auto found = imports().find(key);
		return found == imports().end() ? 0 : found->second;
	}

	bool inspect_native_image(const void* symbol, std::vector<native_image_range>& ranges, std::string& error)
	{
		ranges.clear();
		error.clear();
		Dl_info owner{};
		if (!symbol || !dladdr(symbol, &owner) || !owner.dli_fbase)
		{
			error = "Native module has no loaded image owner";
			return false;
		}
		const auto* header = static_cast<const mach_header_64*>(owner.dli_fbase);
		if (header->magic != MH_MAGIC_64 || header->sizeofcmds > 16 * 1024 * 1024)
		{
			error = "Unsupported native module image";
			return false;
		}
		auto* commands = reinterpret_cast<const unsigned char*>(header + 1);
		std::uintptr_t slide = 0;
		bool has_base = false;
		std::vector<segment_command_64> segments;
		std::size_t offset = 0;
		for (std::uint32_t i = 0; i < header->ncmds; ++i)
		{
			if (offset > header->sizeofcmds || header->sizeofcmds - offset < sizeof(load_command))
			{
				error = "Truncated native image commands";
				return false;
			}
			load_command command;
			std::memcpy(&command, commands + offset, sizeof(command));
			if (command.cmdsize < sizeof(command) || command.cmdsize > header->sizeofcmds - offset)
			{
				error = "Invalid native image command size";
				return false;
			}
			if (command.cmd == LC_SEGMENT_64)
			{
				if (command.cmdsize < sizeof(segment_command_64))
				{
					error = "Truncated native image segment";
					return false;
				}
				segment_command_64 segment;
				std::memcpy(&segment, commands + offset, sizeof(segment));
				if (segment.fileoff == 0 && segment.filesize >= sizeof(*header) + header->sizeofcmds)
				{
					slide = reinterpret_cast<std::uintptr_t>(header) - segment.vmaddr;
					has_base = true;
				}
				segments.push_back(segment);
			}
			offset += command.cmdsize;
		}
		if (!has_base)
		{
			error = "Native image base segment is missing";
			return false;
		}
		for (const auto& segment : segments)
		{
			if (!segment.vmsize || !segment.initprot)
				continue;
			const auto start = slide + segment.vmaddr;
			if (segment.vmsize > UINTPTR_MAX - start || ((segment.initprot & VM_PROT_WRITE) && (segment.initprot & VM_PROT_EXECUTE)))
			{
				error = "Invalid native image memory permissions";
				ranges.clear();
				return false;
			}
			// dyld may have made DATA_CONST read-only after applying fixups.
			// Use current VM permissions, not the segment's initial permissions.
			for (vm_address_t cursor = start, end = start + segment.vmsize; cursor < end;)
			{
				vm_address_t region = cursor;
				vm_size_t size = 0;
				vm_region_basic_info_data_64_t info{};
				mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
				mach_port_t object = MACH_PORT_NULL;
				const auto result = vm_region_64(mach_task_self(), &region, &size, VM_REGION_BASIC_INFO_64,
					reinterpret_cast<vm_region_info_t>(&info), &count, &object);
				if (object != MACH_PORT_NULL)
					mach_port_deallocate(mach_task_self(), object);
				if (result != KERN_SUCCESS || region > cursor || !size || size > UINTPTR_MAX - region ||
					region + size <= cursor || ranges.size() >= 8192)
				{
					error = "Native image segment is not fully mapped";
					ranges.clear();
					return false;
				}
				const auto limit = std::min<vm_address_t>(end, region + size);
				ranges.push_back({cursor, static_cast<std::size_t>(limit - cursor),
					bool(info.protection & VM_PROT_READ), bool(info.protection & VM_PROT_WRITE), bool(info.protection & VM_PROT_EXECUTE)});
				cursor = limit;
			}
		}
		return !ranges.empty();
	}

	bool validate_native_module(const native_module_descriptor* module,
		std::span<const native_image_range> ranges, std::string& error)
	{
		error.clear();
		const auto range = [&](const void* pointer, std::size_t count, std::size_t stride, std::size_t alignment, int permission)
		{
			const auto address = reinterpret_cast<std::uintptr_t>(pointer);
			if (!address || address % alignment || count > SIZE_MAX / stride)
				return false;
			const auto size = count * stride;
			return std::ranges::any_of(ranges, [&](const auto& part)
				{
					return address >= part.address && address - part.address <= part.size && size <= part.size - (address - part.address) &&
				           part.readable && (permission != 1 || part.writable) && (permission != 2 || part.executable) &&
				           !(part.writable && part.executable);
				});
		};
		const auto fail = [&](const char* message)
		{
			error = message;
			return false;
		};
		if (!range(module, 1, sizeof(*module), alignof(native_module_descriptor), 0))
			return fail("Native descriptor is outside its image");
		if (module->size != sizeof(*module) || module->version != native_module_version || module->reserved ||
			!module->entry_count || module->entry_count > 1048576 || module->import_count > 16384 || module->return_count > 65536)
			return fail("Incompatible native module descriptor");
		if (!range(module->entries, module->entry_count, sizeof(native_module_entry), alignof(native_module_entry), 0) ||
			!range(module->import_table, 1, sizeof(void*), alignof(void*), 1) ||
			(module->import_count && !range(module->imports, module->import_count, sizeof(char*), alignof(char*), 0)) ||
			(module->return_count && !range(module->returns, module->return_count, sizeof(native_module_returns), alignof(native_module_returns), 0)))
			return fail("Native module tables are outside their image");
		for (std::uint32_t i = 0; i < module->entry_count; ++i)
		{
			const auto& entry = module->entries[i];
			if ((entry.kind != 1 && entry.kind != 2) || entry.address % 4 || entry.reserved ||
				(entry.kind == 2 && entry.address >= 0x40000) || !range(entry.target, 1, 4, 4, 2))
				return fail("Invalid native compiled entry");
		}
		for (std::uint32_t i = 0; i < module->import_count; ++i)
		{
			const auto* name = module->imports[i];
			std::size_t length = 0;
			for (; length < 8192; ++length)
			{
				const auto address = reinterpret_cast<std::uintptr_t>(name);
				if (length > UINTPTR_MAX - address || !range(reinterpret_cast<const void*>(address + length), 1, 1, 1, 0))
					return fail("Native import name is outside its image");
				if (!name[length])
					break;
			}
			if (!length || length == 8192 || !resolve_native_import({name, length}))
				return fail("Unknown native runtime import");
		}
		for (std::uint32_t i = 0; i < module->return_count; ++i)
		{
			const auto& item = module->returns[i];
			if (item.reserved || item.chunk_count > 65536 ||
				!range(item.base, 1, 4, 4, 1) || !range(item.count, 1, 4, 4, 1) ||
				!range(item.targets, 1, sizeof(void*), alignof(void*), 1) ||
				(item.chunk_count && !range(item.chunks, item.chunk_count, sizeof(void*), alignof(void*), 0)))
				return fail("Invalid native return bindings");
			for (std::uint32_t j = 0; j < item.chunk_count; ++j)
				if (!range(item.chunks[j], 1, 4, 4, 2))
					return fail("Native return target is outside executable image memory");
		}
		return true;
	}

	bool native_loaded_module::admit(const native_module_descriptor* module, std::span<const native_image_range> ranges,
		native_return_registry& returns, std::string& error)
	{
		error.clear();
		if (m_descriptor)
		{
			if (module == m_descriptor)
				return true;
			error = "Loaded native module identity cannot change";
			return false;
		}
		if (!validate_native_module(module, ranges, error))
			return false;
		std::vector<void*> imports;
		imports.reserve(module->import_count);
		for (std::uint32_t i = 0; i < module->import_count; ++i)
			imports.push_back(reinterpret_cast<void*>(resolve_native_import(module->imports[i])));
		std::vector<native_return_binding> bindings;
		bindings.reserve(module->return_count);
		for (std::uint32_t i = 0; i < module->return_count; ++i)
		{
			const auto& item = module->returns[i];
			bindings.push_back({item.base, item.count, item.targets, {item.chunks, item.chunk_count}});
		}
		if (!returns.append(bindings))
		{
			error = "Conflicting native return bindings";
			return false;
		}
		// There are no throwing operations after the first publication.
		m_imports.swap(imports);
		*module->import_table = m_imports.data();
		m_descriptor = module;
		return true;
	}

	bool native_return_registry::append(std::span<const native_return_binding> programs)
	{
		if (programs.empty())
			return true;
		std::size_t count = m_targets.size();
		for (std::size_t i = 0; i < programs.size(); ++i)
		{
			const auto& program = programs[i];
			if (!program.base || !program.count || !program.targets ||
				program.chunks.size() > std::numeric_limits<std::uint32_t>::max() - count ||
				std::ranges::find(program.chunks, nullptr) != program.chunks.end())
				return false;
			const auto overlaps = [&](const native_return_binding& other)
			{
				return program.base == other.base || program.base == other.count ||
				       program.count == other.base || program.count == other.count || program.targets == other.targets;
			};
			if (program.base == program.count || std::ranges::any_of(m_bindings, overlaps) ||
				std::ranges::any_of(programs.first(i), overlaps))
				return false;
			count += program.chunks.size();
		}

		// Complete all throwing operations before publishing bindings. Preserve
		// previous numeric tokens even when the backing data table moves.
		auto targets = m_targets;
		auto bindings = m_bindings;
		targets.reserve(count);
		bindings.reserve(bindings.size() + programs.size());
		for (const auto& program : programs)
		{
			targets.insert(targets.end(), program.chunks.begin(), program.chunks.end());
			bindings.push_back(program);
		}
		std::uint32_t base = static_cast<std::uint32_t>(m_targets.size());
		m_targets.swap(targets);
		m_bindings.swap(bindings);
		for (const auto& program : programs)
		{
			*program.base = program.chunks.empty() ? 0 : base;
			base += static_cast<std::uint32_t>(program.chunks.size());
		}
		for (const auto& program : m_bindings)
		{
			*program.count = size();
			*program.targets = m_targets.data();
		}
		return true;
	}
} // namespace rpcs3::ios::aot
