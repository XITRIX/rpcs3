#include "AOTService.h"
#include "AOTCompiler.h"
#include "AOTDispatch.h"
#include "AOTInputs.h"
#include "AOTAnalysis.h"
#include "AOTNativeRuntime.h"
#include "AOTNativeGateway.h"
#include "AOTCounters.h"
#include "Emu/Cell/lv2/sys_sync.h"
#include "Emu/Cell/PPUAnalyser.h"
#include "Emu/Cell/PPUThread.h"
#include "Emu/Memory/vm.h"
#include "Emu/system_utils.hpp"
#include "AOTRecompiler.h"
#include "AOTPreparation.h"
#include "Loader/ISO.h"
#include "Crypto/unself.h"
#include "RPCS3IOSSettings.h"
#include "RPCS3IOSPerformance.h"
#include "Emu/system_config.h"
#include "Emu/system_progress.hpp"
#include "Emu/system_utils.hpp"
#include "Utilities/File.h"
#include "util/logs.hpp"
#include "Crypto/sha256.h"
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#endif
#include "Utilities/JITIOS.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#ifdef RPCS3_AOT_HOST_TESTS
#include <iostream>
#endif
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <dlfcn.h>

LOG_CHANNEL(aot_log, "AOT");

namespace rpcs3::ios::aot
{
	namespace
	{
		namespace path = std::filesystem;
		std::string root, signer, core_digest, active_title;
		std::atomic<bool> cancelled;
		std::atomic<std::uint32_t> stage, completed, total;
		std::mutex progress_mutex;
		std::string preparation_title;
		std::map<std::string, std::pair<std::uint32_t, std::string>> title_progress;
		struct loaded_module
		{
			void* handle = nullptr; // Intentionally never dlclose: executable lifetime is the process.
			std::string path;
			std::vector<const void*> imports;
			bool registered = false;
			native_loaded_module native;
		};
		std::map<std::string, loaded_module> loaded_modules;
		native_return_registry native_returns;
		native_sign_callback native_signer = nullptr;
		void* native_signer_context = nullptr;
		native_ppu_table native_ppu_targets;
		native_return_registry native_ppu_returns;
		std::map<std::string, loaded_module> native_ppu_images;
		runtime_counters native_ppu_counts;
		std::atomic<u64> native_ppu_compiled{0};

		bool hex(const std::string& value, std::size_t size)
		{
			return value.size() == size && std::all_of(value.begin(), value.end(), [](char c)
											   {
												   return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
											   });
		}
		std::string nonce()
		{
			std::random_device source;
			std::ostringstream result;
			for (unsigned i = 0; i < 4; ++i)
				result << std::hex << std::setfill('0') << std::setw(8) << std::uint32_t(source());
			return result.str();
		}
		const std::string process_nonce = nonce();

		void require(bool value, const char* message)
		{
			if (!value)
				throw failure{message};
		}
		void checkpoint()
		{
			require(!cancelled.load(), "AOT preparation cancelled");
		}

		struct hash
		{
#ifdef __APPLE__
			// System SHA-256 uses the available CPU acceleration. Hash every byte just
			// as before; neither file timestamps nor a previous boot replace integrity.
			CC_SHA256_CTX context{};
			hash()
			{
				require(CC_SHA256_Init(&context), "SHA-256 failed");
			}
			void append(const void* data, std::size_t size)
			{
				auto* bytes = static_cast<const unsigned char*>(data);
				while (size)
				{
					const auto count = static_cast<CC_LONG>(std::min<std::size_t>(size, UINT32_MAX));
					require(CC_SHA256_Update(&context, bytes, count), "SHA-256 failed");
					bytes += count;
					size -= count;
				}
			}
#else
			mbedtls_sha256_context context{};
			hash()
			{
				mbedtls_sha256_init(&context);
				mbedtls_sha256_starts_ret(&context, 0);
			}
			~hash()
			{
				mbedtls_sha256_free(&context);
			}
			void append(const void* data, std::size_t size)
			{
				require(!mbedtls_sha256_update_ret(&context, static_cast<const unsigned char*>(data), size), "SHA-256 failed");
			}
#endif
			void append(const std::string& value)
			{
				append(value.data(), value.size());
				const char separator = 0;
				append(&separator, 1);
			}
			std::string finish()
			{
				unsigned char digest[32];
#ifdef __APPLE__
				require(CC_SHA256_Final(digest, &context), "SHA-256 failed");
#else
				require(!mbedtls_sha256_finish_ret(&context, digest), "SHA-256 failed");
#endif
				std::ostringstream result;
				for (const auto byte : digest)
					result << std::hex << std::setfill('0') << std::setw(2) << unsigned(byte);
				return result.str();
			}
		};

		std::string hash_file(const path::path& file, bool cancellable = false)
		{
			require(path::is_regular_file(path::symlink_status(file)), "AOT source is not a regular file");
			const auto size = path::file_size(file);
			const auto modified = path::last_write_time(file);
			std::ifstream stream(file, std::ios::binary);
			require(bool(stream), "Cannot read AOT source");
			hash digest;
			std::array<char, 65536> bytes;
			while (stream)
			{
				if (cancellable)
					checkpoint();
				stream.read(bytes.data(), bytes.size());
				digest.append(bytes.data(), stream.gcount());
			}
			require(stream.eof() && path::file_size(file) == size && path::last_write_time(file) == modified, "AOT source changed while hashing");
			return digest.finish();
		}

		void write_bytes(const path::path& file, const char* data, std::size_t size)
		{
			path::create_directories(file.parent_path());
			fs::pending_file pending(file.string());
			require(bool(pending.file), "Cannot create pending AOT file");
			require(pending.file.write(data, size) == size, "Insufficient storage while saving AOT data");
			require(pending.commit(), "Cannot commit AOT data");
		}
		void sync_directory(const path::path& directory)
		{
			fs::file handle(directory.string(), fs::read);
			require(bool(handle), "Cannot open AOT directory for synchronization");
			handle.sync();
		}
		void write_text(const path::path& file, const std::string& value)
		{
			write_bytes(file, value.data(), value.size());
			sync_directory(file.parent_path());
		}
		std::string read_text(const path::path& file, std::size_t limit = 128 * 1024 * 1024)
		{
			require(path::is_regular_file(path::symlink_status(file)) && path::file_size(file) <= limit, "Invalid AOT metadata file");
			std::ifstream stream(file, std::ios::binary);
			require(bool(stream), "Cannot read AOT metadata");
			return {std::istreambuf_iterator<char>(stream), {}};
		}

		path::path title_root(const std::string& title)
		{
			require(!root.empty() && is_valid_game_title_id(title), "Invalid AOT title or unconfigured signing");
			const auto result = path::path(root) / title;
			path::create_directories(result);
			require(path::weakly_canonical(result).parent_path() == path::weakly_canonical(root), "AOT title path escapes its cache");
			return result;
		}

		path::path generation_root(const std::string& title, const std::string& generation)
		{
			require(hex(generation, 32), "Invalid AOT generation identity");
			const auto directory = title_root(title);
			const auto result = directory / generation;
			require(path::is_directory(path::symlink_status(result)) &&
						path::weakly_canonical(result).parent_path() == path::weakly_canonical(directory),
				"Invalid AOT generation path");
			return result;
		}

		// A transaction hashes its inputs once, independently of the compiler revision.
		// Old observations can then be checked against their original exact core key
		// without rescanning all source bytes for each historical revision.
		std::string input_binding(const installed_game& game, bool cancellable)
		{
			hash digest;
			digest.append("rpcs3-aot-inputs-v4");
			digest.append(game.title_id);
			digest.append(game.version);
			cfg_root config;
			bool custom = false;
			require(load_effective_settings(config, game.title_id, custom) == settings_load_error::none, "Cannot validate effective AOT settings");
			normalize_ppu_llvm_settings(config);
			// The native translators/analyzers consume Core, strict rendering's MFC
			// policy, and save-state compatibility. Keep all Core options conservatively;
			// audio, UI and unrelated renderer settings do not affect CPU output.
			digest.append(config.core.to_string());
			digest.append(config.video.strict_rendering_mode.to_string());
			digest.append(config.savestate.compatible_mode.to_string());
			digest.append(config.savestate.state_inspection_mode.to_string());
			const auto target = native_object_options(config.core.llvm_cpu.to_string());
			digest.append(target.cpu);
			digest.append(target.features);
			std::map<std::string, path::path> sources;
			const auto collect = [&](const char* role, const path::path& source, bool all)
			{
				std::vector<path::path> files;
				collect_inputs(source, all, files, cancellable ? std::function<void()>(checkpoint) : std::function<void()>{});
				const bool directory = path::is_directory(source);
				for (const auto& file : files)
				{
					// A source role and module-relative path survive application-container
					// relocation. File contents are still authenticated on every launch.
					const auto relative = directory ? file.lexically_relative(source).generic_string() : std::string("@file");
					sources.emplace(std::string(role) + "/" + relative, file);
				}
			};
			collect("game", game.path, false);
			const auto updates = path::path(fs::get_config_dir()) / "dev_hdd0/game" / game.title_id;
			if (path::weakly_canonical(updates) != path::weakly_canonical(game.path))
				collect("updates", updates, false);
			// Update selection depends on title/category, even when executable bytes
			// remain unchanged. Authenticate that decision with the compiled inputs.
			collect("update-metadata", updates / "PARAM.SFO", true);
			collect("patches", path::path(fs::get_config_dir()) / "patches", true);
			collect("patch-config", path::path(fs::get_config_dir()) / "patch_config.yml", true);
			for (const auto& [identity, file] : sources)
			{
				digest.append(identity);
				digest.append(hash_file(file, cancellable));
			}
			return digest.finish();
		}
		std::string bind_inputs(const std::string& inputs, const std::string& compiler_core, bool native = native_ppu_enabled())
		{
			hash digest;
			digest.append("rpcs3-aot-native-llvm-inputs-v4");
			digest.append(inputs);
			digest.append(compiler_core);
			if (native)
				digest.append("loader-native-ppu-v1");
			return digest.finish();
		}
		std::string binding(const installed_game& game, bool cancellable, const std::string& compiler_core = core_digest)
		{
			return bind_inputs(input_binding(game, cancellable), compiler_core);
		}

		// Bounded, core-owned metadata. Swift handles only opaque generation IDs and
		// the two explicitly resolved signing paths, never this serialization/layout.
		struct manifest
		{
			std::string title, generation, creator, binding, signer, core, signed_digest;
			std::vector<instruction> entries;
			std::uint32_t translated_ppu = 0, translated_spu = 0;
			std::vector<std::pair<std::uint32_t, std::uint32_t>> translated_modules;
			std::vector<std::uint32_t> module_ends; // Optional legacy-compatible explicit boundaries.
		};
		constexpr std::size_t maximum_entries = 1048576;
		constexpr std::size_t module_batch_size = 512;
		constexpr std::size_t maximum_modules = (maximum_entries + module_batch_size - 1) / module_batch_size;
		std::size_t module_count(const manifest& value)
		{
			return value.module_ends.empty() ? (value.entries.size() + module_batch_size - 1) / module_batch_size : value.module_ends.size();
		}
		std::pair<std::size_t, std::size_t> module_range(const manifest& value, std::size_t index)
		{
			const auto first = value.module_ends.empty() ? index * module_batch_size : index ? value.module_ends[index - 1] :
			                                                                                   0;
			const auto last = value.module_ends.empty() ? std::min(first + module_batch_size, value.entries.size()) : value.module_ends[index];
			return {first, last - first};
		}
		void validate_partitions(const manifest& value)
		{
			require(value.module_ends.size() <= maximum_modules, "Too many AOT module partitions");
			std::size_t first = 0;
			for (const auto end : value.module_ends)
			{
				require(end > first && end <= value.entries.size() && end - first <= module_batch_size, "Invalid AOT module partition");
				first = end;
			}
			require(value.module_ends.empty() || first == value.entries.size(), "Incomplete AOT module partitions");
		}

		void u32(std::string& bytes, std::uint32_t value)
		{
			for (unsigned i = 0; i < 4; ++i)
				bytes.push_back(char(value >> (i * 8)));
		}
		void string(std::string& bytes, const std::string& value)
		{
			u32(bytes, value.size());
			bytes.append(value);
		}
		std::size_t manifest_size(const manifest& value)
		{
			validate_partitions(value);
			constexpr std::size_t limit = 128 * 1024 * 1024;
			require(value.entries.size() <= maximum_entries && value.translated_modules.size() <= maximum_modules, "AOT metadata exceeds the supported entry/module limit");
			std::size_t bytes = 8, words = 0;
			const auto add = [&](std::size_t count)
			{
				require(count <= limit - bytes, "AOT metadata exceeds the supported 128 MiB size");
				bytes += count;
			};
			for (const auto* field : {&value.title, &value.generation, &value.creator, &value.binding, &value.signer, &value.core, &value.signed_digest})
			{
				add(4);
				add(field->size());
			}
			add(4);
			for (const auto& entry : value.entries)
			{
				require(entry.words.size() <= 65536 && entry.blocks.size() <= 65536 && entry.words.size() <= 16777216 - words,
					"AOT metadata exceeds the supported guest-word limit");
				words += entry.words.size();
				add(24 + entry.words.size() * 4 + entry.blocks.size() * 8);
			}
			add(8);
			if (!value.translated_modules.empty() || !value.module_ends.empty())
				add(4 + value.translated_modules.size() * 8);
			if (!value.module_ends.empty())
				add(4 + value.module_ends.size() * 4);
			return bytes;
		}
		std::string encode(const manifest& value)
		{
			std::string bytes;
			bytes.reserve(manifest_size(value));
			bytes = value.module_ends.empty() ? "RAOT0004" : "RAOT0005";
			for (const auto* field : {&value.title, &value.generation, &value.creator, &value.binding, &value.signer, &value.core, &value.signed_digest})
				string(bytes, *field);
			u32(bytes, value.entries.size());
			for (const auto& entry : value.entries)
			{
				u32(bytes, entry.guest_kind);
				u32(bytes, entry.opcode);
				u32(bytes, entry.words.size());
				u32(bytes, entry.address);
				u32(bytes, entry.entry_offset);
				u32(bytes, entry.blocks.size());
				for (const auto& [offset, size] : entry.blocks)
				{
					u32(bytes, offset);
					u32(bytes, size);
				}
				for (const auto word : entry.words)
					u32(bytes, word);
			}
			u32(bytes, value.translated_ppu);
			u32(bytes, value.translated_spu);
			if (!value.translated_modules.empty() || !value.module_ends.empty())
			{
				u32(bytes, value.translated_modules.size());
				for (const auto& [ppu, spu] : value.translated_modules)
				{
					u32(bytes, ppu);
					u32(bytes, spu);
				}
			}
			if (!value.module_ends.empty())
			{
				u32(bytes, value.module_ends.size());
				for (const auto end : value.module_ends)
					u32(bytes, end);
			}
			return bytes;
		}
		manifest decode(const std::string& bytes)
		{
			const bool partitioned = bytes.starts_with("RAOT0005");
			require(partitioned || bytes.starts_with("RAOT0004"), "Unsupported AOT metadata");
			std::size_t cursor = 8;
			auto integer = [&]()
			{
				require(cursor <= bytes.size() && bytes.size() - cursor >= 4, "Truncated AOT metadata");
				std::uint32_t result = 0;
				for (unsigned i = 0; i < 4; ++i)
					result |= std::uint32_t(static_cast<unsigned char>(bytes[cursor++])) << (i * 8);
				return result;
			};
			auto text = [&]()
			{
				const auto count = integer();
				require(count <= 128 && cursor <= bytes.size() && count <= bytes.size() - cursor, "Invalid AOT metadata string");
				const auto value = bytes.substr(cursor, count);
				cursor += count;
				return value;
			};
			manifest value;
			for (auto* field : {&value.title, &value.generation, &value.creator, &value.binding, &value.signer, &value.core, &value.signed_digest})
				*field = text();
			require(is_valid_game_title_id(value.title) && hex(value.generation, 32) && hex(value.creator, 32) && hex(value.binding, 64) &&
						hex(value.signer, 64) && hex(value.core, 64) && (value.signed_digest.empty() || hex(value.signed_digest, 64)),
				"Invalid AOT metadata identity");
			const auto count = integer();
			require(count <= 1048576, "Too many AOT entries");
			std::size_t word_count = 0;
			for (std::uint32_t i = 0; i < count; ++i)
			{
				const auto kind = integer();
				const auto op = integer();
				const auto size = integer();
				require((kind == 1 || kind == 2) && size <= 65536 && (word_count += size) <= 16777216, "Invalid native AOT code unit");
				instruction unit{kind, op, {}};
				unit.address = integer();
				unit.entry_offset = integer();
				require(unit.entry_offset < std::max(1u, size), "Invalid native AOT entry offset");
				const auto blocks = integer();
				require(blocks <= size, "Too many native PPU blocks");
				for (std::uint32_t b = 0; b < blocks; ++b)
				{
					const auto offset = integer(), length = integer();
					require(length && offset < size && length <= size - offset, "Invalid native PPU block");
					unit.blocks.emplace_back(offset, length);
				}
				for (std::uint32_t j = 0; j < size; ++j)
					unit.words.push_back(integer());
				require(unit.words.empty() || unit.words[unit.entry_offset] == op, "Invalid AOT unit identity");
				value.entries.push_back(std::move(unit));
			}
			value.translated_ppu = integer();
			value.translated_spu = integer();
			require(std::uint64_t(value.translated_ppu) + value.translated_spu <= count, "Invalid native AOT compilation counts");
			if (cursor != bytes.size())
			{
				const auto modules = integer();
				require(modules <= maximum_modules, "Invalid native AOT module counts");
				for (std::uint32_t part = 0; part < modules; ++part)
				{
					const auto p = integer(), s = integer();
					value.translated_modules.emplace_back(p, s);
				}
				if (cursor != bytes.size())
				{
					require(partitioned, "Unexpected AOT partition table");
					const auto partitions = integer();
					require(partitions && partitions <= maximum_modules, "Invalid AOT partition table");
					for (std::uint32_t part = 0; part < partitions; ++part)
						value.module_ends.push_back(integer());
				}
				validate_partitions(value);
				require(modules == module_count(value) || (modules == 0 && !value.module_ends.empty() &&
															  value.translated_ppu == 0 && value.translated_spu == 0),
					"Invalid native AOT module counts");
				std::uint64_t ppu = 0, spu = 0;
				for (std::uint32_t part = 0; part < modules; ++part)
				{
					const auto [p, s] = value.translated_modules[part];
					const auto [first, size] = module_range(value, part);
					const auto ppu_count = std::count_if(value.entries.begin() + first, value.entries.begin() + first + size,
						[](const auto& entry)
						{
							return entry.guest_kind == 1;
						});
					require(p <= ppu_count && s <= size - ppu_count, "Invalid native AOT translated module count");
					ppu += p;
					spu += s;
				}
				require(ppu == value.translated_ppu && spu == value.translated_spu, "Invalid native AOT aggregate counts");
			}
			require(!partitioned || !value.module_ends.empty(), "Missing AOT partition table");
			require(cursor == bytes.size(), "Trailing AOT metadata");
			return value;
		}

		struct signing_job
		{
			manifest value;
			std::size_t next_module = 0;
			std::vector<std::string> signed_digests;
		};
		std::map<std::string, signing_job> signing_jobs;
		std::string module_name(const char* prefix, std::size_t index, const char* extension)
		{
			return std::string(prefix) + "-" + std::to_string(index) + extension;
		}
		std::vector<std::string> module_digests(const manifest& value, const path::path& directory, bool cancellable = false)
		{
			require(!value.entries.empty() && module_count(value) <= maximum_modules, "Invalid AOT digest table module count");
			const auto bytes = read_text(directory / "digests.txt", 65 * maximum_modules);
			hash digest;
			digest.append(bytes);
			require(digest.finish() == value.signed_digest && bytes.size() == module_count(value) * 65, "AOT module digest table is damaged");
			std::vector<std::string> result;
			for (std::size_t i = 0; i < module_count(value); ++i)
			{
				const auto item = bytes.substr(i * 65, 64);
				require(hex(item, 64) && bytes[i * 65 + 64] == '\n', "Invalid AOT module digest");
				const auto image = directory / module_name("signed", i, ".dylib");
				require(path::file_size(image) <= 8 * 1024 * 1024, "Prepared AOT library exceeds the supported size");
				require(hash_file(image, cancellable) == item, "Prepared AOT library is damaged");
				result.push_back(item);
			}
			return result;
		}
		std::string discovery_binding;

		void progress(const std::string& title, std::uint32_t value, const std::string& detail)
		{
			std::lock_guard lock(progress_mutex);
			stage = value;
			title_progress[title] = {value, detail};
		}
	} // namespace

	namespace
	{
		path::path cache_directory(const std::string& title)
		{
			require(is_valid_game_title_id(title), "Invalid AOT cache title");
			const auto base = root.empty() ? path::path(fs::get_cache_dir()) / "AOT/v4" : path::path(root);
			for (const auto& directory : {base.parent_path(), base, base / title})
				require(!path::is_symlink(path::symlink_status(directory)), "Symlinked AOT cache is unsupported");
			return base / title;
		}
		bool generation_is_loaded(const path::path& generation)
		{
			return std::any_of(loaded_modules.begin(), loaded_modules.end(), [&](const auto& item)
				{
					return item.second.handle && path::path(item.second.path).parent_path() == generation;
				});
		}
		void collect_garbage(const std::string& title, bool clear = false)
		{
			const auto directory = cache_directory(title);
			if (!path::exists(directory))
				return;
			std::set<std::string> keep;
			if (!clear)
			{
				for (const auto* pointer : {"current", "previous", "admitted", "admission.pending"})
					if (path::exists(directory / pointer))
					{
						const auto id = read_text(directory / pointer, 32);
						if (!hex(id, 32))
							return; // Corrupt ownership metadata requires inspection, not deletion.
						keep.insert(id);
					}
				for (const auto& [id, job] : signing_jobs)
					if (job.value.title == title)
						keep.insert(id);
			}
			std::set<path::path> mapped_images;
			for (const auto& [digest, image] : loaded_modules)
				if (image.handle)
					mapped_images.insert(path::path(image.path));
			for (const auto& [digest, image] : native_ppu_images)
				if (image.handle)
					mapped_images.insert(path::path(image.path));
			bool deferred = false;
			const auto native = directory / "native-ppu";
			if (path::exists(native))
			{
				require(!path::is_symlink(path::symlink_status(native)), "Symlinked native PPU cache is unsupported");
				for (const auto& key : path::directory_iterator(native))
				{
					if (!hex(key.path().filename().string(), 64) || !path::is_directory(key.symlink_status()))
						continue;
					std::set<std::string> retained;
					if (!clear)
						for (const auto* marker : {"current", "prepared", "admission.pending"})
							if (path::exists(key.path() / marker))
							{
								const auto value = read_text(key.path() / marker, 32);
								require(hex(value, 32), "Invalid native PPU cache ownership");
								retained.insert(value);
							}
					for (const auto& generation : path::directory_iterator(key.path()))
					{
						const auto id = generation.path().filename().string();
						if (!hex(id, 32) || !path::is_directory(generation.symlink_status()) || retained.contains(id))
							continue;
						for (const auto& file : path::directory_iterator(generation.path()))
						{
							if (mapped_images.contains(file.path()))
							{
								deferred = true;
								continue;
							}
							if (path::is_regular_file(file.symlink_status()))
								path::remove(file.path());
						}
						if (path::is_empty(generation.path()))
							path::remove(generation.path());
					}
					if (clear)
						for (const auto* marker : {"current", "prepared", "admission.pending"})
							path::remove(key.path() / marker);
					if (path::is_empty(key.path()))
						path::remove(key.path());
				}
				if (path::is_empty(native))
					path::remove(native);
			}
			for (const auto& item : path::directory_iterator(directory))
			{
				const auto id = item.path().filename().string();
				if (!hex(id, 32) || !path::is_directory(item.symlink_status()))
					continue;
				const bool loaded = generation_is_loaded(item.path());
				deferred |= loaded;
				if (!keep.contains(id) && !loaded)
				{
					path::remove_all(item.path());
					continue;
				}
				// Incremental preparation reuses signed images and the manifest only.
				// Once signed, objects and unsigned copies have no remaining owner.
				if (path::exists(item.path() / "digests.txt"))
					for (const auto& file : path::directory_iterator(item.path()))
					{
						const auto name = file.path().filename().string();
						if (!path::is_regular_file(file.symlink_status()))
							continue;
						const bool intermediate = (name.starts_with("module-") && name.ends_with(".o")) ||
						                          (name.starts_with("unsigned-") && name.ends_with(".dylib"));
						const bool signed_image = name.size() > 13 && name.starts_with("signed-") && name.ends_with(".dylib") &&
						                          std::all_of(name.begin() + 7, name.end() - 6, [](char byte)
													  {
														  return byte >= '0' && byte <= '9';
													  });
						// An obsolete generation may be kept solely for a few mapped
						// originals. Copied images with no handle have no remaining owner.
						if (intermediate || (!keep.contains(id) && signed_image && !mapped_images.contains(file.path())))
							path::remove(file.path());
					}
			}
			if (clear)
			{
				for (const auto* name : {"current", "previous", "admitted", "admission.pending", "discoveries.bin", "last-session.json"})
					path::remove(directory / name);
				if (deferred)
					write_text(directory / "clear.pending", "Loaded images are retained until process exit");
				else
					path::remove(directory / "clear.pending");
			}
			sync_directory(directory);
		}
	} // namespace

	std::uint64_t cache_size(const std::string& title)
	{
		const auto directory = cache_directory(title);
		if (!path::exists(directory))
			return 0;
		std::uint64_t size = 0;
		for (const auto& item : path::recursive_directory_iterator(directory))
		{
			require(!item.is_symlink(), "Symlinked AOT cache is unsupported");
			if (item.is_regular_file())
			{
				const auto bytes = item.file_size();
				require(bytes <= UINT64_MAX - size, "AOT cache exceeds supported size");
				size += bytes;
			}
		}
		return size;
	}
	std::uint64_t clear_cache(const std::string& title)
	{
		const auto before = cache_size(title);
		collect_garbage(title, true);
		const auto after = cache_size(title);
		return before > after ? before - after : 0;
	}

	void configure(const std::string& cache_root, const std::string& identity, const std::string& core)
	{
		require(hex(identity, 64) && hex(core, 64) && path::path(cache_root).is_absolute(), "Invalid AOT runtime signing identity");
		std::lock_guard lock(progress_mutex);
		require(root.empty() || (signer == identity && core_digest == core), "AOT runtime identity is fixed until relaunch");
		root = (path::path(cache_root) / "AOT/v4").string();
		signer = identity;
		core_digest = core;
		for (const auto& directory : {path::path(cache_root) / "AOT", path::path(root)})
			require(!path::is_symlink(path::symlink_status(directory)), "Symlinked AOT cache is unsupported");
		path::create_directories(root);
		for (const auto& directory : path::directory_iterator(root))
			if (path::is_directory(directory.symlink_status()) && is_valid_game_title_id(directory.path().filename().string()))
				collect_garbage(directory.path().filename().string(), path::exists(directory.path() / "clear.pending"));
	}

	namespace
	{
		// A persistent marker is committed before entering dyld or module code. If the
		// process dies there, the next launch quarantines that exact immutable image.
		void quarantine_interrupted_admission(const path::path& directory)
		{
			const auto marker = directory / "admission.pending";
			if (!path::exists(marker))
				return;
			const auto generation = read_text(marker, 32);
			if (hex(generation, 32) && path::is_directory(path::symlink_status(directory / generation)))
				write_text(directory / generation / "rejected.txt", "Previous execution admission was interrupted; this generation will not be retried automatically");
			path::remove(marker);
			sync_directory(directory);
		}
		std::string admission_error()
		{
			try
			{
				throw;
			}
			catch (const failure& error)
			{
				return error.message.substr(0, 512);
			}
			catch (const std::exception& error)
			{
				return std::string(error.what()).substr(0, 512);
			}
			catch (...)
			{
				return "Unrecognized execution admission failure";
			}
		}
		void load_generation(const installed_game& game, const manifest& selected, const std::vector<std::string>& verified_digests)
		{
			const auto& generation = selected.generation;
			const auto generation_path = generation_root(game.title_id, generation);
			std::vector<rpcs3_aot_entry_v2> compiled_entries;
			std::vector<std::uint32_t> compiled_origins;
			std::vector<native_spu_selection> native_selections;
			for (std::size_t part = 0; part < module_count(selected); ++part)
			{
				const auto library = (generation_path / module_name("signed", part, ".dylib")).string();
				auto& loaded = loaded_modules[verified_digests.at(part)];
				if (!loaded.handle)
				{
					loaded.path = library;
					loaded.handle = dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
				}
				if (!loaded.handle)
				{
					const std::string error = std::string("OS rejected the prepared AOT library: ") + dlerror();
					loaded_modules.erase(verified_digests.at(part));
					throw failure{error};
				}
				void* handle = loaded.handle;
				auto descriptor = reinterpret_cast<const rpcs3_aot_module_v2* (*)()>(dlsym(handle, "rpcs3_aot_module_v2"));
				auto registration = reinterpret_cast<std::uint32_t (*)(const rpcs3_aot_runtime_context_v2*)>(dlsym(handle, "rpcs3_aot_register_runtime_v2"));
				require(descriptor && registration, "AOT module runtime exports are missing");
				const auto* module = descriptor();
				require(module && module->structure_size == sizeof(*module) && module->version == RPCS3_AOT_RUNTIME_VERSION &&
							module->import_count <= 4096 && !module->import_reserved && (module->import_count == 0 || module->imports),
					"Invalid native AOT runtime interface");
				const auto* native = static_cast<const native_module_descriptor*>(dlsym(handle, "rpcs3_aot_native_module"));
				if (native)
				{
					std::vector<native_image_range> ranges;
					std::string error;
					require(inspect_native_image(native, ranges, error), error.c_str());
					require(validate_native_module(native, ranges, error), error.c_str());
					require(native->import_count == module->import_count && native->imports == module->imports,
						"Native and compatibility import descriptors disagree");
				}
				if (!loaded.registered && !native)
				{
					auto& runtime = loaded.imports;
					runtime.clear();
					for (std::uint32_t i = 0; i < module->import_count; ++i)
					{
						const auto* name = module->imports[i];
						require(name && strnlen(name, 129) <= 128, "Invalid native AOT runtime symbol");
						const auto address = resolve_runtime_symbol(name);
						require(address != 0, "Native AOT runtime helper is unavailable");
						runtime.push_back(reinterpret_cast<const void*>(address));
					}
					rpcs3_aot_runtime_context_v2 context{};
					context.structure_size = sizeof(context);
					context.version = RPCS3_AOT_RUNTIME_VERSION;
					context.runtime_functions = runtime.data();
					context.runtime_function_count = runtime.size();
					require(registration(&context) == 1, "AOT runtime version is incompatible");
					loaded.registered = true;
				}
				// Resolve authenticated image segments once per library. Per-entry
				// dladdr scans all loaded images and becomes quadratic for large games.
				std::vector<native_image_range> image_ranges;
				std::string image_error;
				require(inspect_native_image(reinterpret_cast<const void*>(descriptor), image_ranges, image_error), image_error.c_str());
				const auto owns_executable = [&](const void* target)
				{
					const auto address = reinterpret_cast<std::uintptr_t>(target);
					return std::any_of(image_ranges.begin(), image_ranges.end(), [&](const auto& range)
						{
							return range.executable && address >= range.address && address - range.address < range.size;
						});
				};
				const auto [first, count] = module_range(selected, part);
				require(module && module->structure_size == sizeof(*module) && module->version == RPCS3_AOT_RUNTIME_VERSION &&
							module->entry_count == count && module->reserved == 0 && module->entries,
					"Invalid AOT module descriptor");
				std::uint32_t ppu_entries = 0, spu_entries = 0;
				std::map<std::uint32_t, void*> native_targets;
				if (native)
				{
					for (std::uint32_t i = 0; i < native->entry_count; ++i)
					{
						const auto& entry = native->entries[i];
						require(entry.kind == 2 && entry.identity_index < count, "Native entry manifest index is invalid");
						const auto& expected = selected.entries[first + entry.identity_index];
						const std::uint32_t origin = expected.address || !expected.blocks.empty() ? expected.address : 0x100;
						require(expected.guest_kind == 2 && entry.address == origin + expected.entry_offset * 4 &&
									native_targets.emplace(entry.identity_index, entry.target).second,
							"Native code coordinates disagree with manifest");
					}
				}
				for (std::size_t i = 0; i < count; ++i)
				{
					const auto& entry = module->entries[i];
					const auto& expected = selected.entries[first + i];
					const auto words = expected.words.empty() ? std::span<const std::uint32_t>(&expected.opcode, 1) : std::span<const std::uint32_t>(expected.words);
					require(entry.guest_kind == expected.guest_kind && entry.opcode == expected.opcode && entry.word_count == words.size() &&
								entry.entry_offset == expected.entry_offset && entry.words && std::equal(words.begin(), words.end(), entry.words),
						"AOT code identity is invalid");
					require(entry.verification_flags == (expected.guest_kind == 2 && !expected.blocks.empty() ? RPCS3_AOT_SPARSE_SPU : 0), "Invalid native AOT verification flags");
					require(entry.alternate_count <= entry.word_count && (!entry.alternate_count || entry.alternate_offsets), "Invalid native AOT entry table");
					for (std::uint32_t e = 0; e < entry.alternate_count; ++e)
						require(entry.alternate_offsets[e] < entry.word_count, "Invalid native AOT entry offset");
					if (const auto target = native_targets.find(i); target != native_targets.end())
					{
						require(!entry.invoke, "Native entry also exposes a fragment target");
						native_selections.push_back({entry, target->second});
						++spu_entries;
					}
					if (entry.invoke)
					{
						require(owns_executable(reinterpret_cast<const void*>(entry.invoke)), "AOT entry owner is invalid");
						compiled_entries.push_back(entry);
						compiled_origins.push_back(expected.blocks.empty() ? UINT32_MAX : expected.address);
						(entry.guest_kind == 1 ? ppu_entries : spu_entries)++;
					}
				}
				if (native)
				{
					std::vector<native_image_range> ranges;
					std::string error;
					require(inspect_native_image(native, ranges, error), error.c_str());
					require(loaded.native.admit(native, ranges, native_returns, error), error.c_str());
					loaded.registered = true;
				}
				if (!selected.translated_modules.empty())
					require(selected.translated_modules[part] == std::pair(ppu_entries, spu_entries), "AOT module translation counts are invalid");
			}
			require(std::count_if(compiled_entries.begin(), compiled_entries.end(), [](const auto& entry)
						{
							return entry.guest_kind == 1;
						}) == selected.translated_ppu &&
						std::count_if(compiled_entries.begin(), compiled_entries.end(), [](const auto& entry)
							{
								return entry.guest_kind == 2;
							}) + native_selections.size() ==
							selected.translated_spu,
				"Native AOT entry counts do not match the signed generation");
			auto pending_entries = discoveries();
			select_entries(compiled_entries, compiled_origins);
			require(select_native_spu_entries(native_selections), "Cannot select native SPU programs");
			restore_discoveries(pending_entries);
		}
	} // namespace

	// The loader supplies actual relocated/patched metadata. Signing is delegated
	// synchronously to a wrapper callback on this worker, never through the locked
	// lifecycle API. The wrapper may await foreground/Keychain readiness independently.
	void set_native_signer(native_sign_callback callback, void* context)
	{
		require(callback && !native_signer, "Native AOT signer is missing or already registered");
		native_signer_context = context;
		native_signer = callback;
	}
	bool native_ppu_enabled()
	{
		return native_signer != nullptr;
	}

	namespace
	{
		// Publication uses RPCS3's existing CPU safepoint, including dynamic PRX loads.
		// No native table or import storage changes while a CPU executes signed code.
		template <typename F>
		void publish_native_ppu(F&& operation)
		{
			std::exception_ptr error;
			auto* current = cpu_thread::get_current();
			// LV2 module-loading syscalls already marked their PPU as waiting. Use
			// the external-work path so suspend_all preserves that existing state.
			if (current && (current->state & cpu_flag::wait))
				current = nullptr;
			cpu_thread::suspend_all(current, {}, [&]
				{
					try
					{
						operation();
					}
					catch (...)
					{
						error = std::current_exception();
					}
				});
			if (error)
				std::rethrow_exception(error);
		}
	} // namespace
	void register_native_ppu_range(std::uint32_t address, std::uint32_t size)
	{
		if (!native_ppu_enabled() || !size)
			return;
		const u64 end = (u64(address) + size + 0xffff) & ~0xffffull;
		const std::uint32_t start = address & ~0xffffu;
		require(end <= 0x100000000ull && end - start <= UINT32_MAX, "Invalid native PPU executable range");
		publish_native_ppu([&]
			{
				require(native_ppu_targets.register_range(start, end - start), "Cannot reserve native PPU dispatch data");
			});
	}

	void record_native_ppu_entry()
	{
		thread_local const auto counts = native_ppu_counts.register_worker();
		counts.add(2);
	}

	void record_native_ppu_fallback()
	{
		thread_local const auto counts = native_ppu_counts.register_worker();
		counts.add(1);
	}

	bool execute_native_ppu(ppu_thread& cpu)
	{
		auto* target = native_ppu_targets.target(cpu.cia);
		if (!target || target == reinterpret_cast<void*>(&rpcs3_aot_native_ppu_fallback))
			return false;
		const auto segment = *reinterpret_cast<const u16*>(native_ppu_targets.base() + vm::g_exec_addr_seg_offset + cpu.cia / 2);
		const std::array<u64, 7> arguments{reinterpret_cast<u64>(native_ppu_targets.base()), reinterpret_cast<u64>(&cpu),
			u64(segment) << 13, reinterpret_cast<u64>(vm::g_base_addr), cpu.gpr[0], cpu.gpr[1], cpu.gpr[2]};
		thread_local const auto counts = native_ppu_counts.register_worker();
		counts.add(0);
		thread_local native_ppu_guard_context guard;
		guard.pages = native_ppu_page_registry();
		guard.dispatch_epoch = ppu_aot_dispatch_epoch();
		guard.verified_entries = counts.counter(2);
		guard.verification_misses = counts.counter(3);
		struct restore_guard
		{
			ppu_thread& cpu;
			native_ppu_guard_context* previous;
			~restore_guard()
			{
				cpu.aot_native_context = previous;
			}
		} restore{cpu, cpu.aot_native_context};
		cpu.aot_native_context = &guard;
		invoke_native(target, arguments, &cpu, cpu.hv_ctx);
		return true;
	}

	void prepare_native_ppu(const ppu_module<lv2_obj>& info)
	{
		if (!native_ppu_enabled() || info.segs.empty())
			return;
		// XMB/firmware boots have no title-owned artifact binding. A failed input
		// validation must likewise remain interpreted, never compile unbound code.
		if (active_title.empty() || !hex(discovery_binding, 64))
			return;
		checkpoint();
		info.prepare_compilation_attributes(g_cfg.core.ppu_set_sat_bit.get());
		auto options = native_object_options();
#if defined(RPCS3_AOT_HOST_TESTS) && !defined(RPCS3_IOS_AOT_SIMULATOR)
		options.platform = object_platform::host_macos;
#endif
		const std::uint32_t relocation = info.is_relocatable ? info.segs.front().addr : 0;
		const auto functions = info.get_funcs();
		usz position = 0;
		std::atomic<unsigned> unavailable{0};
		struct native_work
		{
			ppu_module<lv2_obj> part;
			usz count;
			path::path artifact;
			std::string digest;
			bool fresh = true, ready = false;
		};
		std::vector<native_work> work;
		completed = 0;
		total = functions.size();
		while (position < functions.size())
		{
			checkpoint();
			const auto part_start = position;
			ppu_module<lv2_obj> part;
			part.copy_next_compilation_part(info, position, [](const ppu_function&)
				{
					return false;
				});
			work.push_back({std::move(part), position - part_start});
		}
		// Use the same dialog/counters as the native LLVM loader. Its renderer,
		// observational boot API and background Live Activity share this progress.
		scoped_progress_dialog dialog("Preparing signed PPU modules");
		struct module_progress
		{
			const std::uint32_t total;
			atomic_t<std::uint32_t> done{0};

			explicit module_progress(std::uint32_t count) : total(count)
			{
				g_progr_ptotal += total;
			}

			void advance()
			{
				++done;
				++g_progr_pdone;
			}

			~module_progress()
			{
				// Cancellation/failed partitions must not strand the native dialog.
				g_progr_pdone += total - done.load();
			}
		} module_work(static_cast<std::uint32_t>(work.size()));
		// Hash immutable loader metadata once; copying the SHA state preserves
		// every input in each partition identity without rewalking relocations.
		hash common_identity;
		common_identity.append("native-ppu-loader-v2");
		common_identity.append(discovery_binding);
		common_identity.append(core_digest);
		common_identity.append(signer);
		common_identity.append(options.cpu);
		common_identity.append(options.features);
		const auto scalar = [&](auto value)
		{
			common_identity.append(&value, sizeof(value));
		};
		scalar(info.is_relocatable);
		for (unsigned bit = 0; bit < unsigned(ppu_attr::__bitset_enum_max); ++bit)
			scalar(info.attr.test(ppu_attr(bit)));
		scalar(info.segs.size());
		for (const auto& segment : info.segs)
		{
			scalar(segment.addr);
			scalar(segment.size);
			scalar(segment.type);
			scalar(segment.flags);
			scalar(segment.filesz);
		}
		scalar(info.get_relocs().size());
		for (const auto& reloc : info.get_relocs())
		{
			scalar(reloc.addr);
			scalar(reloc.type);
			scalar(reloc.data);
		}
		scalar(info.stub_addr_to_constant_state_of_registers.size());
		for (const auto& [address, values] : info.stub_addr_to_constant_state_of_registers)
		{
			scalar(address);
			scalar(values.size());
			for (const auto& [mask, value] : values)
			{
				scalar(mask.mask);
				scalar(value);
			}
		}
		std::atomic<std::uint64_t> compile_us{0}, link_us{0}, sign_us{0};
		const auto elapsed = [](auto start)
		{
			return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
		};
		const auto started = std::chrono::steady_clock::now();
		const auto prepare = [&](std::size_t index)
		{
			checkpoint();
			auto& item = work[index];
			const auto& part = item.part;
			if (part.get_funcs().empty())
				return;
			auto identity = common_identity;
			const auto scalar = [&](auto value)
			{
				identity.append(&value, sizeof(value));
			};
			scalar(part.local_bounds.first);
			scalar(part.local_bounds.second);
			for (const auto& function : part.get_funcs())
			{
				scalar(function.addr);
				scalar(function.size);
				scalar(function.toc);
				scalar(function.blocks.size());
				for (const auto& [address, size] : function)
				{
					scalar(address);
					scalar(size);
					const auto* bytes = info.get_ptr<const u8>(address, size);
					require(bytes || !size, "Native PPU code range is unavailable");
					if (size)
						identity.append(bytes, size);
				}
			}
			const auto key = identity.finish();
			const auto directory = title_root(active_title) / "native-ppu" / key;
			path::create_directories(directory);
			if (path::exists(directory / "admission.pending"))
			{
				aot_log.error("Interrupted native PPU admission; retaining interpreter fallback for %s", key);
				++unavailable;
				return;
			}
			auto& artifact = item.artifact;
			auto& digest = item.digest;
			const auto pointer = path::exists(directory / "current") ? "current" : "prepared";
			if (path::exists(directory / pointer))
			{
				const auto generation = read_text(directory / pointer, 32);
				require(hex(generation, 32), "Invalid native PPU generation");
				artifact = directory / generation;
				require(!path::exists(artifact / "admission.pending"), "Previous native PPU admission was interrupted");
				digest = read_text(artifact / "digest", 64);
				require(hex(digest, 64) && hash_file(artifact / "signed.dylib") == digest, "Native PPU artifact changed");
				item.fresh = pointer != std::string_view("current");
			}
			else
			{
				artifact = directory / nonce();
				require(path::create_directory(artifact), "Cannot create native PPU artifact directory");
				progress(active_title, 2, "Compiling native PPU functions: " + std::to_string(completed.load()) + " of " + std::to_string(functions.size()) + " ready");
				const auto compile_start = std::chrono::steady_clock::now();
				std::vector<char> object;
				std::string error;
				if (!emit_ppu_native(part, options, object, error, []
						{
							return cancelled.load();
						}))
				{
					checkpoint();
					aot_log.error("Native PPU partition declined; native interpreter retained: %s", error);
					++unavailable;
					return;
				}
				compile_us += elapsed(compile_start);
				write_bytes((artifact / "module.o").string(), object.data(), object.size());
			}
			item.ready = true;
		};
		const auto sign = [&](std::size_t index)
		{
			checkpoint();
			auto& item = work[index];
			const auto& artifact = item.artifact;
			auto& digest = item.digest;
			if (item.ready && digest.empty())
			{
				std::string error;
				const auto directory = artifact.parent_path();
				const auto key = directory.filename().string();
				const auto object_path = (artifact / "module.o").string();
				const auto link_start = std::chrono::steady_clock::now();
				const std::array objects{object_path};
				const std::array<std::string, 1> exports{"_rpcs3_aot_native_module"};
				const auto input = (artifact / "unsigned.dylib").string();
				const auto output = (artifact / "signed.dylib").string();
				require(link_module(objects, input, "@rpath/rpcs3-aot-native-ppu-" + key + ".dylib", options.platform, error, exports), error.c_str());
				link_us += elapsed(link_start);
				const auto sign_start = std::chrono::steady_clock::now();
				progress(active_title, 4, "Signing native PPU functions");
				std::array<char, 1024> signed_hash{};
				const auto signed_status = native_signer(native_signer_context, key.c_str(), input.c_str(), output.c_str(), signed_hash.data(), signed_hash.size());
				signed_hash.back() = 0;
				require(signed_status == 0, ("Wrapper could not sign native PPU code: " + std::string(signed_hash.data())).c_str());
				checkpoint();
				require(signed_hash.back() == 0, "Invalid native PPU signing response");
				digest = signed_hash.data();
				require(hex(digest, 64) && hash_file(output) == digest, "Signed native PPU digest mismatch");
				sign_us += elapsed(sign_start);
				write_text(artifact / "digest", digest);
				// Preserve signed, hash-verified output across cancellation before
				// caller-owned admission. It is never an executable dispatch target.
				write_text(directory / "prepared", artifact.filename().string());
				path::remove(artifact / "module.o");
				path::remove(artifact / "unsigned.dylib");
			}
			completed.fetch_add(item.count);
			module_work.advance();
		};
		// Keep LLVM workers compiling while the serialized linker/signer drains
		// completed objects. Admission stays on the initiating CPU after joining.
		const auto workers = std::min(rpcs3::utils::get_max_threads(), automatic_llvm_compile_threads);
		preparation_pipeline(work.size(), workers, prepare, sign);
#ifdef RPCS3_IOS
		if (compile_us.load())
			rpcs3::ios::relieve_process_memory_pressure();
#endif
		aot_log.notice("Native PPU preparation: %u partitions, %u workers, wall=%llu us, compiler=%llu us, linker=%llu us, signing/verification=%llu us (worker times overlap)",
			work.size(), workers, elapsed(started), compile_us.load(), link_us.load(), sign_us.load());
		dialog = "Loading signed PPU modules";
		for (const auto& item : work)
		{
			checkpoint();
			if (!item.ready)
			{
				continue;
			}
			const auto& part = item.part;
			const auto& artifact = item.artifact;
			const auto& digest = item.digest;
			const auto directory = artifact.parent_path();
			progress(active_title, 5, "Loading signed native PPU functions");
			auto& loaded = native_ppu_images[digest];
			// The worker still hashes the complete file on every boot. A retained,
			// admitted image with that exact digest needs only fresh guest binding.
			const bool retained = loaded.handle && loaded.registered;
			const native_module_descriptor* descriptor = retained ? loaded.native.descriptor() : nullptr;
			void* handle = loaded.handle;
			std::vector<native_image_range> ranges;
			std::string error;
			if (!retained)
			{
				write_text(directory / "admission.pending", artifact.filename().string());
				write_text(artifact / "admission.pending", process_nonce);
				// Never unload an admitted image, including after a failed admission.
				handle = dlopen((artifact / "signed.dylib").c_str(), RTLD_NOW | RTLD_LOCAL);
				require(handle, "OS rejected signed native PPU library");
				descriptor = static_cast<const native_module_descriptor*>(dlsym(handle, "rpcs3_aot_native_module"));
				require(inspect_native_image(descriptor, ranges, error), error.c_str());
				require(validate_native_module(descriptor, ranges, error), error.c_str());
			}
			require(!descriptor->return_count, "PPU module unexpectedly declares SPU return tokens");
			std::set<std::uint32_t> expected;
			for (const auto& function : part.get_funcs())
				if (function.size)
					expected.insert(function.addr - relocation);
			require(descriptor->entry_count == expected.size(), "Native PPU function count mismatch");
			for (std::uint32_t i = 0; i < descriptor->entry_count; ++i)
			{
				const auto& entry = descriptor->entries[i];
				require(entry.kind == 1 && expected.erase(entry.address) == 1, "Native PPU coordinates disagree with loader");
			}
			publish_native_ppu([&]
				{
					if (!loaded.handle)
					{
						loaded.handle = handle;
						loaded.path = (artifact / "signed.dylib").string();
						require(loaded.native.admit(descriptor, ranges, native_ppu_returns, error), error.c_str());
						loaded.registered = true;
					}
					require(loaded.registered, "Native PPU image previously failed admission");
					for (std::uint32_t i = 0; i < descriptor->entry_count; ++i)
					{
						const auto& entry = descriptor->entries[i];
						const std::uint32_t address = entry.address + relocation;
						const bool added = native_ppu_targets.target(address) == reinterpret_cast<void*>(&rpcs3_aot_native_ppu_fallback);
						require(native_ppu_targets.bind(address, info.segs.front().addr, entry.target), "Native PPU target binding failed");
						if (added)
							native_ppu_compiled.fetch_add(1);
						ppu_register_native_aot_function(address);
					}
				});
			if (!retained)
			{
				path::remove(artifact / "admission.pending");
				path::remove(directory / "admission.pending");
			}
			if (item.fresh)
			{
				write_text(directory / "current", artifact.filename().string());
				path::remove(directory / "prepared");
			}
		}
		progress(active_title, 6, unavailable ? "Some native PPU partitions are unavailable; interpreter fallback active" : "Native PPU functions and SPU programs active");
	}

	void begin_game(const installed_game& game)
	{
		native_ppu_targets.reset();
		native_ppu_counts.reset();
		native_ppu_compiled = 0;
		clear_runtime_spu_programs();
		select_entries({});
		{
			std::lock_guard lock(progress_mutex);
			active_title = game.title_id;
		}
		discovery_binding.clear();
		progress(game.title_id, 1, "Validating game contents and settings");
		const auto directory = title_root(game.title_id);
		std::string rejected;
		try
		{
			quarantine_interrupted_admission(directory);
			discovery_binding = binding(game, false);
			const auto discovery_file = directory / "discoveries.bin";
			if (path::exists(discovery_file))
			{
				try
				{
					const auto previous = decode(read_text(discovery_file));
					if (previous.binding == discovery_binding && previous.core == core_digest)
						restore_discoveries(previous.entries);
				}
				catch (...)
				{ /* damaged observations cannot enable native code */
				}
			}
			const auto observed = discoveries();
			std::set<std::string> attempted;
			for (const auto* pointer : {"current", "previous", "admitted"})
			{
				if (!path::exists(directory / pointer))
					continue;
				std::string generation;
				try
				{
					generation = read_text(directory / pointer, 32);
					if (!attempted.insert(generation).second)
						continue;
					const auto artifact = generation_root(game.title_id, generation);
					if (path::exists(artifact / "rejected.txt"))
						throw failure{read_text(artifact / "rejected.txt", 1024)};
					const auto selected = decode(read_text(artifact / "manifest.bin"));
					require(selected.generation == generation && selected.binding == discovery_binding && selected.signer == signer &&
								selected.core == core_digest && selected.title == game.title_id,
						"Prepared code is incompatible with current inputs, signing identity or core");
					const auto verified_digests = module_digests(selected, artifact); // Verify every copied image before entering dyld.
					write_text(directory / "admission.pending", generation);
					try
					{
						load_generation(game, selected, verified_digests);
					}
					catch (...)
					{
						// Keep partial handles and import storage alive even on rejection.
						write_text(artifact / "rejected.txt", admission_error());
						path::remove(directory / "admission.pending");
						sync_directory(directory);
						throw;
					}
					path::remove(directory / "admission.pending");
					sync_directory(directory);
					write_text(directory / "admitted", generation);
					progress(game.title_id, 6, pointer != std::string_view("current") ? "Previous compatible code active after rejection; unknown code uses the interpreter" : "Prepared code active; unknown code uses the interpreter");
					return;
				}
				catch (const failure& error)
				{
					if (rejected.empty())
						rejected = error.message;
				}
				catch (const std::exception& error)
				{
					if (rejected.empty())
						rejected = error.what();
				}
				select_entries({});
				restore_discoveries(observed);
			}
		}
		catch (const failure& error)
		{
			rejected = error.message;
		}
		catch (const std::exception& error)
		{
			rejected = error.what();
		}
		catch (...)
		{
			rejected = "Unrecognized execution admission failure";
		}
		if (rejected.size() > 512)
			rejected.resize(512);
		progress(game.title_id, rejected.empty() ? 0 : 8, rejected.empty() ? "No prepared code; interpreter fallback is collecting discoveries" : "AOT unavailable; interpreter fallback: " + rejected);
	}

	void clear_game()
	{
		native_ppu_targets.reset();
		native_ppu_counts.reset();
		native_ppu_compiled = 0;
		clear_runtime_spu_programs();
		select_entries({});
		std::lock_guard lock(progress_mutex);
		active_title.clear();
		discovery_binding.clear();
	}

	void finish_game()
	{
		if (active_title.empty() || root.empty() || !hex(discovery_binding, 64))
			return;
		instruction_inventory native_spu;
		collect_runtime_spu_programs(native_spu);
		std::vector<instruction> entries;
		for (const auto& [unit, count] : native_spu)
		{
			(void)count;
			entries.push_back(unit);
		}

		// Unsupported observations remain in counters; supported discoveries extend
		// automatic preparation on the next launch without a manual action.
		for (const auto& entry : discoveries())
			if (supported(entry))
				entries.push_back(entry);
		manifest value{active_title, nonce(), process_nonce, discovery_binding, signer, core_digest, {}, std::move(entries)};
		write_text(title_root(active_title) / "discoveries.bin", encode(value));
		write_text(title_root(active_title) / "last-session.json", status_json(active_title));
	}

	namespace
	{
		std::vector<instruction> analyze_game(const installed_game& game)
		{
			instruction_inventory inventory;
			const auto updates = path::path(fs::get_config_dir()) / "dev_hdd0/game" / game.title_id;
			const auto update_metadata = psf::load_object((updates / "PARAM.SFO").string());
			// Match normal disc startup: a validated installed update replaces only
			// the disc boot executable. Other disc modules remain discoverable.
			const bool updated_boot = path::is_regular_file(updates / "USRDIR/EBOOT.BIN") &&
			                          psf::get_string(update_metadata, "TITLE_ID") == game.title_id &&
			                          psf::get_string(update_metadata, "CATEGORY") == "GD";
			std::uint32_t files = 0, images = 0;
			const auto scan = [&](fs::file input)
			{
				checkpoint();
				if (!input || input.size() < 4)
					return;
				require(++files <= 8192, "Too many AOT executable files");
				std::array<std::uint8_t, 4> magic{};
				require(input.read_at(0, magic.data(), magic.size()) == magic.size(), "Cannot read AOT executable header");
				const bool self = magic == std::array<std::uint8_t, 4>{'S', 'C', 'E', 0};
				const bool elf = magic == std::array<std::uint8_t, 4>{0x7f, 'E', 'L', 'F'};
				if (!self && !elf)
					return;
				require(input.size() <= 512 * 1024 * 1024, "AOT executable exceeds the supported analysis size");
				if (self)
					input = decrypt_self(input);
				// A module requiring a key learned only during guest execution stays on
				// the interpreter; it must not prevent other validated code preparation.
				if (!input)
					return;
				require(input.size() <= 512 * 1024 * 1024, "Decrypted AOT executable is too large");
				std::vector<std::uint8_t> bytes(input.size());
				require(input.read_at(0, bytes.data(), bytes.size()) == bytes.size(), "Cannot read AOT executable code");
				const auto result = analyze_elf(bytes, inventory, checkpoint, !native_ppu_enabled());
				images += result.ppu_images + result.spu_images;
				progress(game.title_id, 1, "Analyzing executable code (" + std::to_string(images) + " PPU/SPU images)");
			};
			const auto directory = [&](const path::path& source)
			{
				std::vector<path::path> files;
				collect_inputs(source, false, files, checkpoint);
				for (const auto& file : files)
				{
					const auto relative = file.lexically_relative(source).generic_string();
					if (updated_boot && path::weakly_canonical(source) != path::weakly_canonical(updates) &&
						(relative == "PS3_GAME/USRDIR/EBOOT.BIN" || relative == "USRDIR/EBOOT.BIN"))
						continue;
					scan(fs::file(file.string()));
				}
			};
			if (path::is_regular_file(game.path))
			{
				iso_archive archive(game.path);
				require(archive.is_valid() && archive.get_key_status() == iso_key_status::OK, "Cannot read installed disc image for AOT analysis");
				std::size_t visited = 0;
				std::function<void(const iso_fs_node&, const std::string&)> walk;
				walk = [&](const iso_fs_node& node, const std::string& name)
				{
					checkpoint();
					require(++visited <= 1000000, "AOT disc directory is too large");
					if (!node.metadata.is_directory)
					{
						if (executable_input_name(name) && !(updated_boot && name == "PS3_GAME/USRDIR/EBOOT.BIN"))
							scan(fs::file(archive.open(name)));
						return;
					}
					for (const auto& child : node.children)
						walk(*child, name.empty() ? child->metadata.name : name + "/" + child->metadata.name);
				};
				const auto* content = archive.retrieve("PS3_GAME");
				require(content, "Installed disc has no PS3_GAME directory");
				walk(*content, "PS3_GAME");
			}
			else
				directory(game.path);
			if (path::weakly_canonical(updates) != path::weakly_canonical(game.path))
				directory(updates);
			std::vector<instruction> result;
			for (const auto& [unit, frequency] : inventory)
			{
				(void)frequency;
				result.push_back(unit);
			}
			return result;
		}
		bool entry_less(const instruction& a, const instruction& b)
		{
			return a < b;
		}
		bool compatible_inventory(const manifest& previous, const manifest& next)
		{
			return previous.title == next.title && previous.binding == next.binding &&
			       previous.signer == next.signer && previous.core == next.core &&
			       std::set<instruction>(previous.entries.begin(), previous.entries.end()).size() == previous.entries.size();
		}
		bool compatible_observations(const manifest& previous, const manifest& next, const std::string& verified_binding)
		{
			// Only raw guest words may survive a compiler/core update. The caller
			// recomputes the old binding from today's game/settings/patch/CPU inputs.
			// Executable/module reuse still requires compatible_inventory().
			return previous.title == next.title && previous.signer == next.signer && previous.binding == verified_binding;
		}
		void collect_observations(const manifest& previous, std::vector<instruction>& observed)
		{
			instruction_inventory checked;
			for (const auto& entry : previous.entries)
			{
				checkpoint();
				if (native_ppu_enabled() && entry.guest_kind == 1)
					continue;
				// Native function/program inventories are immutable raw guest metadata.
				// Recompilation reruns native SPU analysis and the native PPU translator;
				// no previous machine code or helper address crosses a core update.
				if (entry.guest_kind == 2 && entry.address && entry.blocks.empty())
				{
					require(entry.address < 0x40000 && entry.words.size() <= (0x40000 - entry.address) / 4 && entry.entry_offset < entry.words.size(), "Invalid native SPU observation");
					std::vector<be_t<std::uint32_t>> local_store(0x40000 / 4);
					for (usz i = 0; i < entry.words.size(); ++i)
						local_store[entry.address / 4 + i] = entry.words[i];
					collect_spu_program(local_store.data(), entry.address + entry.entry_offset * 4, checked);
				}
				else if (!entry.address && entry.blocks.empty())
				{
					std::vector<be_t<std::uint32_t>> words(std::max<usz>(entry.words.size(), 1));
					if (entry.words.empty())
						words[0] = entry.opcode;
					else
						for (usz i = 0; i < entry.words.size(); ++i)
							words[i] = entry.words[i];
					collect_native_block(entry.guest_kind, words.data(), words.size(), checked);
				}
				else if (supported(entry))
					++checked[entry];
			}
			for (const auto& [unit, frequency] : checked)
			{
				(void)frequency;
				observed.push_back(unit);
			}
		}
		void extend_inventory(std::vector<instruction>& entries, std::span<const instruction> additional)
		{
			// Existing order is the signed module/descriptor order. Append new units
			// without moving earlier batches, so complete modules remain reusable.
			std::set<instruction> known(entries.begin(), entries.end());
			require(known.size() == entries.size(), "Duplicate prepared AOT code identity");
			for (const auto& entry : additional)
				if (known.insert(entry).second)
					entries.push_back(entry);
			require(entries.size() <= maximum_entries, "AOT code inventory exceeds the supported preparation size");
		}
		void plan_modules(manifest& value)
		{
			std::size_t first = value.module_ends.empty() ? 0 : value.module_ends.back();
			while (first < value.entries.size())
			{
				std::size_t end = first, words = 0;
				while (end < value.entries.size() && end - first < module_batch_size)
				{
					const auto size = std::max<std::size_t>(1, value.entries[end].words.size());
					if (end != first && (words >= 8192 || size > 8192 - words))
						break;
					words += size;
					++end;
				}
				require(value.module_ends.size() < maximum_modules, "AOT inventory needs too many signed modules");
				value.module_ends.push_back(end);
				first = end;
			}
			validate_partitions(value);
		}

		std::size_t reusable_module_count(const manifest& value)
		{
			// Keep closed module boundaries, including explicit native declines.
			// Older metadata without per-module counts retains conservative behavior.
			return value.translated_modules.size() == module_count(value) ||
			               std::uint64_t(value.translated_ppu) + value.translated_spu == value.entries.size() ?
			           (value.module_ends.empty() ? value.entries.size() / module_batch_size : module_count(value)) :
			           0;
		}

		std::string rollback_generation(const manifest& job, const path::path& directory)
		{
			for (const auto* pointer : {"admitted", "current", "previous"})
			{
				if (!path::exists(directory / pointer))
					continue;
				try
				{
					const auto id = read_text(directory / pointer, 32);
					const auto source = generation_root(job.title, id);
					const auto candidate = decode(read_text(source / "manifest.bin"));
					if (candidate.generation != id || path::exists(source / "rejected.txt") || !compatible_inventory(candidate, job))
						continue;
					module_digests(candidate, source, true);
					return id;
				}
				catch (...)
				{
					checkpoint(); /* A damaged current/admitted image must not displace a valid rollback. */
				}
			}
			return {};
		}
	} // namespace

	// Called synchronously before the wrapper exposes the new cancellable job.
	// prepare() must not erase a cancellation delivered while its task was queued.
	void begin_preparation()
	{
		cancelled = false;
	}

	std::string prepare(const installed_game& game)
	{
		require(!root.empty(), "Import a validated signing identity before starting AOT mode");
		completed = 0;
		total = 0;
		{
			std::lock_guard lock(progress_mutex);
			preparation_title = game.title_id;
		}
		const auto directory = title_root(game.title_id);
		path::path generation;
		try
		{
			// Discard only unfinished in-memory signing jobs from previous attempts.
			for (const auto& [identifier, pending_job] : signing_jobs)
				path::remove_all(generation_root(pending_job.value.title, identifier));
			signing_jobs.clear();
			progress(game.title_id, 1, "Checking prepared code and game contents");
			const auto inputs = input_binding(game, true);
			quarantine_interrupted_admission(directory);
			collect_garbage(game.title_id);
			path::remove(directory / "clear.pending");
			manifest job{game.title_id, nonce(), process_nonce, bind_inputs(inputs, core_digest), signer, core_digest, {}, {}};
			// The native translators read g_cfg. Compile against the same effective
			// title configuration that is hashed, restoring the stopped core on all
			// paths. No preferences, YAML files or saved decoder choices are changed.
			struct config_scope
			{
				std::string saved = g_cfg.to_string();
				std::string name = g_cfg.name;
				~config_scope()
				{
					g_cfg.from_string(saved);
					g_cfg.name = name;
				}
			} compiler_config;
			bool custom = false;
			require(load_effective_settings(game.title_id, custom) == settings_load_error::none, "Cannot load AOT compiler settings");
			normalize_ppu_llvm_settings(g_cfg);
			std::vector<instruction> observed;
			std::vector<instruction> retained;
			std::vector<std::string> reused_digests;
			std::vector<std::pair<std::uint32_t, std::uint32_t>> reused_statistics;
			path::path reused_path;
			std::size_t reused_modules = 0;
			std::map<std::string, std::string> checked_bindings{{core_digest, job.binding}};
			const auto observe_previous = [&](const manifest& previous)
			{
				if (previous.title != job.title || previous.signer != job.signer)
					return;
				auto [verified, inserted] = checked_bindings.try_emplace(previous.core);
				if (inserted)
					verified->second = bind_inputs(inputs, previous.core);
				if (compatible_observations(previous, job, verified->second) ||
					(native_ppu_enabled() && compatible_observations(previous, job, bind_inputs(inputs, previous.core, false))))
					collect_observations(previous, observed); // Raw SPU discoveries survive the PPU backend migration; code never does.
			};
			progress(game.title_id, 1, "Reading native SPU code discoveries");
			instruction_inventory native_spu;
			collect_cached_spu_programs(rpcs3::utils::get_cache_dir_by_serial(game.title_id), native_spu, checkpoint);
			for (const auto& [unit, count] : native_spu)
			{
				(void)count;
				observed.push_back(unit);
			}

			const auto discovery_file = directory / "discoveries.bin";
			if (path::exists(discovery_file))
			{
				try
				{
					const auto value = decode(read_text(discovery_file));
					observe_previous(value);
				}
				catch (...)
				{
					checkpoint(); /* damaged optional discoveries cannot enable code */
				}
			}
			if (native_ppu_enabled())
				std::erase_if(observed, [](const auto& entry)
					{
						return entry.guest_kind == 1;
					});
			std::sort(observed.begin(), observed.end(), entry_less);
			observed.erase(std::unique(observed.begin(), observed.end()), observed.end());
			if (path::exists(directory / "current"))
			{
				try
				{
					const auto current = read_text(directory / "current", 32);
					const auto cached_path = generation_root(game.title_id, current);
					auto cached = decode(read_text(cached_path / "manifest.bin"));
					if (cached.generation == current && compatible_inventory(cached, job))
					{
						if (path::exists(cached_path / "rejected.txt"))
						{
							progress(game.title_id, 8, "Prepared generation was rejected; using compatible previous code or interpreter fallback");
							return {}; // An automatic launch is not permission to retry a failed image.
						}
						auto verified_digests = module_digests(cached, cached_path, true);
						auto ordered = cached.entries;
						std::sort(ordered.begin(), ordered.end(), entry_less);
						if (std::includes(ordered.begin(), ordered.end(), observed.begin(), observed.end(), entry_less))
						{
							checkpoint();
							progress(game.title_id, 6, "Prepared code verified; starting game");
							aot_log.notice("%s: reusing all %u signed modules; no compilation or signing", game.title_id, module_count(cached));
							return {}; // Cached generation needs no signing or recompilation.
						}
						// Stop saves only observations still missing from the active
						// generation. Preserve earlier learned code when extending it;
						// otherwise alternating runs continually forget each other.
						reused_modules = reusable_module_count(cached);
						for (std::size_t part = 0; part < reused_modules; ++part)
						{
							const auto [first, count] = module_range(cached, part);
							job.module_ends.push_back(first + count);
						}
						reused_statistics = cached.translated_modules;
						reused_path = cached_path;
						reused_digests = std::move(verified_digests);
						retained = std::move(cached.entries);
					}
					else if (cached.generation == current)
					{
						aot_log.notice("%s: rebuilding AOT code (%s)", game.title_id,
							cached.core != job.core ? "core changed" : cached.signer != job.signer ? "signing identity changed" :
																   cached.binding != job.binding   ? "game inputs or compiler settings changed" :
																									 "inventory incompatible");
						observe_previous(cached);
					}
				}
				catch (const failure& error)
				{
					checkpoint();
					aot_log.warning("%s: cached AOT validation failed: %s", game.title_id, error.message);
				}
				catch (const std::exception& error)
				{
					checkpoint();
					aot_log.warning("%s: cached AOT validation failed: %s", game.title_id, error.what());
				}
				catch (...)
				{
					checkpoint();
					aot_log.warning("%s: cached AOT validation failed with an unknown error", game.title_id);
				}
			}
			if (native_ppu_enabled())
				std::erase_if(observed, [](const auto& entry)
					{
						return entry.guest_kind == 1;
					});
			std::sort(observed.begin(), observed.end(), entry_less);
			observed.erase(std::unique(observed.begin(), observed.end()), observed.end());

			if (retained.empty())
			{
				progress(game.title_id, 1, "Analyzing game executables before launch");
				job.entries = analyze_game(game);
			}
			else
			{
				progress(game.title_id, 1, "Extending verified AOT code inventory");
				job.entries = std::move(retained);
			}
			extend_inventory(job.entries, observed);
			if (native_ppu_enabled())
				std::erase_if(job.entries, [](const auto& entry)
					{
						return entry.guest_kind == 1;
					});
			if (job.entries.empty())
			{
				progress(game.title_id, 0, "No supported code available before launch; using native interpreter fallback");
				return {};
			}
			plan_modules(job);
			if (module_count(job) == reused_modules)
			{
				progress(game.title_id, 6, "Prepared code verified; starting game");
				return {};
			}
			(void)manifest_size(job); // Reject oversized inventories before LLVM work.
			const auto reused_entries = reused_modules ? job.module_ends.at(reused_modules - 1) : 0;
			total = job.entries.size() - reused_entries;
			aot_log.notice("%s: retaining %u signed modules (%u entries); compiling %u new modules (%u entries)",
				game.title_id, reused_modules, reused_entries, module_count(job) - reused_modules, total.load());
			generation = directory / job.generation;
			require(path::create_directory(generation), "Cannot create a fresh AOT generation");
			auto options = native_object_options();
			options.native_spu = true;
			struct prepared_part
			{
				std::size_t end;
				object_statistics stats;
				path::path image;
			};
			std::vector<std::vector<prepared_part>> results(module_count(job));
			std::atomic<std::size_t> final_parts{module_count(job)};
			// Bound simultaneous IR/optimizer memory on mobile devices. Lifecycle
			// ownership and signing/publication stay on this serialized caller.
			auto workers = std::max(1u, std::thread::hardware_concurrency());
#ifdef RPCS3_IOS_AOT_SIMULATOR
			// Same-binary controlled serial/parallel preparation on isolated test roots.
			if (const char* requested = std::getenv("RPCS3_AOT_TEST_PREPARATION_WORKERS"))
			{
				require((requested[0] == '1' || requested[0] == '2') && !requested[1], "Invalid Simulator preparation worker count");
				workers = requested[0] - '0';
			}
#endif
			preparation_tasks(module_count(job), workers, [&](std::size_t part)
				{
					checkpoint();
					const auto [first, count] = module_range(job, part);
					if (part < reused_modules)
					{
						progress(game.title_id, 2, "Reusing compiled AOT module " + std::to_string(part + 1) + " of " + std::to_string(module_count(job)));
						const auto destination = generation / module_name("signed", part, ".dylib");
						std::error_code error;
						require(path::copy_file(reused_path / module_name("signed", part, ".dylib"), destination, path::copy_options::none, error) && !error,
							"Cannot copy previously prepared AOT code");
						require(hash_file(destination) == reused_digests.at(part), "Reused AOT code changed after validation");
						fs::file durable(destination.string(), fs::read);
						require(bool(durable), "Cannot synchronize reused AOT module");
						durable.sync();
						object_statistics stats;
						if (!reused_statistics.empty())
						{
							stats.ppu = reused_statistics[part].first;
							stats.spu = reused_statistics[part].second;
						}
						else
							for (const auto& entry : std::span(job.entries).subspan(first, count))
								(entry.guest_kind == 1 ? stats.ppu : stats.spu)++;
						results[part].push_back({first + count, std::move(stats), destination});
						return; // Same immutable signed bytes/identity; only new modules need signing.
					}
					std::vector<bool> visited(count);
					std::size_t work = 0;
					const auto emit = [&](std::size_t begin, std::size_t size, bool descriptors_only)
					{
						checkpoint();
						const auto stem = "work-" + std::to_string(part) + "-" + std::to_string(work++);
						const auto object_file = (generation / (stem + ".o")).string();
						const auto image = generation / (stem + ".dylib");
						object_statistics stats;
						std::string error;
						auto selected_options = options;
						selected_options.descriptors_only = descriptors_only;
						std::size_t previous = 0;
						const auto observe = [&](std::size_t finished)
						{
							checkpoint();
							std::size_t newly_done = 0;
							for (std::size_t i = previous; i < finished; ++i)
								if (!visited[begin - first + i])
								{
									visited[begin - first + i] = true;
									++newly_done;
								}
							completed.fetch_add(newly_done, std::memory_order_relaxed);
							previous = finished;
						};
						progress(game.title_id, 2, "Compiling new AOT module " + std::to_string(part + 1 - reused_modules) + " of " + std::to_string(module_count(job) - reused_modules) + "; reusing " + std::to_string(reused_modules) + " prepared modules");
						{
							std::vector<char> object;
							if (!emit_instructions(std::span(job.entries).subspan(begin, size), selected_options, object, error, &stats, observe))
								throw failure{error};
							write_bytes(object_file, object.data(), object.size());
						}
						checkpoint();
						progress(game.title_id, 3, "Linking new AOT module " + std::to_string(part + 1 - reused_modules) + " of " + std::to_string(module_count(job) - reused_modules));
						const std::string objects[] = {object_file};
						const std::string native_exports[] = {"_rpcs3_aot_native_module", "_rpcs3_aot_module_v2", "_rpcs3_aot_register_runtime_v2"};
						if (!link_module(objects, image.string(), "@rpath/rpcs3-aot-" + job.generation + "-" + stem + ".dylib", options.platform, error,
								stats.spu ? std::span<const std::string>(native_exports) : std::span<const std::string>{}))
							throw failure{error};
						path::remove(object_file);
						checkpoint();
						// The identity is bounded to2MiB; leave3MiB for its CMS and
					    // CodeDirectory within the wrapper's8MiB signed-file limit.
						if (path::file_size(image) > 5 * 1024 * 1024)
						{
							path::remove(image);
							if (size > 1)
								require(final_parts.fetch_add(1) < maximum_modules, "AOT output needs too many signed modules");
							progress(game.title_id, 2, "Splitting a large AOT module before signing");
							return false;
						}
						results[part].push_back({begin + size, std::move(stats), image});
						return true;
					};
					prepare_partition(first, count, emit);
				});
			job.module_ends.clear();
			std::size_t part = 0;
			for (auto& batch : results)
				for (auto& result : batch)
				{
					checkpoint();
					if (part >= reused_modules)
						path::rename(result.image, generation / module_name("unsigned", part, ".dylib"));
					job.module_ends.push_back(result.end);
					job.translated_ppu += result.stats.ppu;
					job.translated_spu += result.stats.spu;
					job.translated_modules.emplace_back(result.stats.ppu, result.stats.spu);
					++part;
				}
			validate_partitions(job);
			write_text(generation / "manifest.bin", encode(job));
			signing_jobs.emplace(job.generation, signing_job{job, reused_modules, {reused_digests.begin(), reused_digests.begin() + reused_modules}});
			progress(game.title_id, 4, "Signing prepared code before game launch");
			return job.generation;
		}
		catch (...)
		{
			if (!generation.empty())
			{
				std::error_code ignored;
				path::remove_all(generation, ignored);
			}
			progress(game.title_id, cancelled ? 7 : 8, "AOT startup preparation did not finish; previous code preserved");
			throw;
		}
	}

	std::uint32_t signing_count(const std::string& identifier)
	{
		const auto found = signing_jobs.find(identifier);
		require(hex(identifier, 32) && found != signing_jobs.end(), "Unknown AOT signing job");
		return module_count(found->second.value) - found->second.next_module;
	}
	std::pair<std::string, std::string> signing_paths(const std::string& identifier)
	{
		const auto found = signing_jobs.find(identifier);
		require(hex(identifier, 32) && found != signing_jobs.end(), "Unknown AOT signing job");
		const auto& job = found->second;
		const auto directory = generation_root(job.value.title, identifier);
		return {(directory / module_name("unsigned", job.next_module, ".dylib")).string(),
			(directory / module_name("signed", job.next_module, ".dylib")).string()};
	}

	void finalize(const std::string& identifier, const std::string& digest)
	{
		require(hex(digest, 64), "Invalid signed AOT digest");
		const auto found = signing_jobs.find(identifier);
		require(found != signing_jobs.end(), "Unknown AOT signing job");
		checkpoint();
		auto& state = found->second;
		auto& job = state.value;
		const auto directory = title_root(job.title);
		const auto generation = generation_root(job.title, identifier);
		require(hash_file(generation / module_name("signed", state.next_module, ".dylib")) == digest, "Signed AOT file changed before publication");
		{
			fs::file signed_file((generation / module_name("signed", state.next_module, ".dylib")).string(), fs::read);
			require(bool(signed_file), "Cannot synchronize signed AOT module");
			signed_file.sync();
		}
		state.signed_digests.push_back(digest);
		++state.next_module;
		if (state.next_module != module_count(job))
			return;
		const auto game = find_installed_game(job.title);
		require(game && job.binding == binding(*game, true), "AOT inputs changed during preparation");
		progress(job.title, 5, "Publishing prepared code");
		std::string digests;
		for (const auto& item : state.signed_digests)
			digests += item + "\n";
		hash aggregate;
		aggregate.append(digests);
		job.signed_digest = aggregate.finish();
		write_text(generation / "digests.txt", digests);
		write_text(generation / "manifest.bin", encode(job));
		module_digests(job, generation, true);
		sync_directory(generation);
		checkpoint();
		if (const auto previous = rollback_generation(job, directory); !previous.empty())
			write_text(directory / "previous", previous);
		write_text(directory / "current", identifier);
		const auto title = job.title;
		signing_jobs.erase(found);
		collect_garbage(title);
		progress(title, 6, "Prepared code ready; starting game");
	}

	void cancel() noexcept
	{
		cancelled = true;
	}
	bool is_cancelled() noexcept
	{
		return cancelled.load();
	}

	std::string status_json(const std::string& title_id)
	{
		require(is_valid_game_title_id(title_id), "Invalid AOT title identity");
		const auto stats = statistics();
		std::string cache;
		std::string detail;
		std::uint32_t current_stage = 0, current_completed = 0, current_total = 0;
		bool active = false;
		{
			std::lock_guard lock(progress_mutex);
			cache = root;
			active = active_title == title_id;
			if (const auto found = title_progress.find(title_id); found != title_progress.end())
			{
				current_stage = found->second.first;
				detail = found->second.second;
			}
			if (title_id == preparation_title)
			{
				current_completed = completed.load();
				current_total = total.load();
			}
		}
		if (current_stage == 1)
		{
			const std::string analysis = g_progr_text;
			if (!analysis.empty())
				detail += " | " + analysis;
		}
		std::replace_if(detail.begin(), detail.end(), [](unsigned char c)
			{
				return c < 0x20;
			},
			' ');
		std::uint64_t saved_ppu = active ? stats.compiled_ppu : 0, saved_spu = active ? stats.compiled_spu : 0, saved_discoveries = 0;
		bool relaunch_required = false;
		// Atomic publication lets observers read bounded metadata independently of
		// the lifecycle lock. Never hash game content or load executable code here.
		if (!cache.empty() && !active && !(current_stage >= 1 && current_stage <= 5))
		{
			try
			{
				const auto directory = path::path(cache) / title_id;
				require(path::weakly_canonical(directory).parent_path() == path::weakly_canonical(cache), "Invalid AOT title cache");
				if (path::exists(directory / "discoveries.bin"))
					saved_discoveries = decode(read_text(directory / "discoveries.bin")).entries.size();
				if (path::exists(directory / "current"))
				{
					const auto generation = read_text(directory / "current", 32);
					require(hex(generation, 32), "Invalid saved AOT generation");
					const auto generation_path = directory / generation;
					require(path::is_directory(path::symlink_status(generation_path)), "Invalid saved AOT generation path");
					const auto saved = decode(read_text(generation_path / "manifest.bin"));
					require(saved.title == title_id && saved.generation == generation && !saved.signed_digest.empty(), "Invalid saved AOT identity");
					saved_ppu = saved.translated_ppu;
					saved_spu = saved.translated_spu;
					relaunch_required = false;
					if (detail.empty())
						detail = "Saved code will be validated against current inputs at boot";
				}
			}
			catch (...)
			{
				if (detail.empty())
					detail = "Saved AOT metadata is damaged; native interpreter remains available";
			}
		}
		if (detail.empty())
			detail = "Code is prepared automatically at the next game start";
		std::ostringstream out;
		out << "{\"stage\":" << current_stage << ",\"completed\":" << current_completed << ",\"total\":" << current_total
			<< ",\"compiled_ppu\":" << (active ? stats.compiled_ppu + native_ppu_compiled.load() : 0) << ",\"compiled_spu\":" << (active ? stats.compiled_spu : 0)
			<< ",\"saved_ppu\":" << saved_ppu << ",\"saved_spu\":" << saved_spu << ",\"saved_discoveries\":" << saved_discoveries
			<< ",\"relaunch_required\":" << (relaunch_required ? "true" : "false")
			<< ",\"executed_ppu\":" << (active ? stats.executed_ppu : 0) << ",\"executed_spu\":" << (active ? stats.executed_spu : 0)
			<< ",\"fallback_ppu\":" << (active ? stats.fallback_ppu + native_ppu_counts.snapshot()[1] : 0) << ",\"fallback_spu\":" << (active ? stats.fallback_spu : 0)
			<< ",\"invocations_ppu\":" << (active ? stats.invocations_ppu : 0)
			<< ",\"invocations_spu\":" << (active ? stats.invocations_spu : 0)
			<< ",\"native_spu_invocations\":" << (active ? stats.native_spu_invocations : 0)
			<< ",\"native_ppu_invocations\":" << (active ? native_ppu_counts.snapshot()[0] : 0)
			<< ",\"native_ppu_verified_entries\":" << (active ? native_ppu_counts.snapshot()[2] : 0)
			<< ",\"native_ppu_verification_misses\":" << (active ? native_ppu_counts.snapshot()[3] : 0)
			<< ",\"retained_dispatch_tables\":" << stats.retained_dispatch_tables
			<< ",\"code_checks\":" << (active ? stats.code_checks : 0)
			<< ",\"code_words_checked\":" << (active ? stats.code_words_checked : 0)
			<< ",\"pending\":" << (active ? stats.pending : 0) << ",\"detail\":" << std::quoted(detail)
			<< ",\"jit_allocation_requests\":" << jit::rejected_executable_requests()
			<< ",\"jit_preparation_requests\":" << jit::rejected_preparation_requests() << "}";
		return out.str();
	}
#ifdef RPCS3_AOT_HOST_TESTS
	void test_native_ppu_loader(const ppu_module<lv2_obj>& info, ppu_thread& cpu, const std::string& output)
	{
		static unsigned signatures = 0, cancel_signature = 0;
		configure(output + "/native-loader-cache", std::string(64, 'a'), std::string(64, 'b'));
		active_title = "TEST00001";
		discovery_binding = std::string(64, 'c');
		begin_preparation();
		set_native_signer(+[](void*, const char*, const char* input, const char* output, char* digest, std::size_t capacity) -> std::int32_t
			{
				path::copy_file(input, output);
				const auto hash = hash_file(output);
				if (capacity <= hash.size())
					return -1;
				std::memcpy(digest, hash.c_str(), hash.size() + 1);
				if (++signatures == cancel_signature)
					cancel();
				return 0;
			},
			nullptr);
		register_native_ppu_range(0x30000, 0x20000);
		prepare_native_ppu(info);
		require(signatures == 1 && native_ppu_compiled == 8, "Native loader lost functions or signing handoff");
		cpu.state.store({});
		cpu.cia = 0x30000;
		cpu.ctr = 4096;
		cpu.lr = 0x40000;
		cpu.gpr[4] = 11;
		cpu.gpr[5] = 17;
		require(execute_native_ppu(cpu) && cpu.cia == 0x40000 && cpu.gpr[4] == 11 + 4096 * 3 && cpu.gpr[5] == 17 + 4096 * 7,
			"Native loader execution disagrees with interpreter fixture");
		cpu.state += cpu_flag::wait;
		const auto retained_image = path::path(native_ppu_images.begin()->second.path);
		const auto retained_directory_time = path::last_write_time(retained_image.parent_path());
		prepare_native_ppu(info);
		require(path::last_write_time(retained_image.parent_path()) == retained_directory_time,
			"Native loader repeated admission writes for a retained image");
		const auto retained_bytes = read_text(retained_image);
		write_text(retained_image, retained_bytes + "tampered");
		bool damaged_rejected = false;
		try
		{
			prepare_native_ppu(info);
		}
		catch (const failure&)
		{
			damaged_rejected = true;
		}
		write_text(retained_image, retained_bytes);
		require(damaged_rejected, "Retained native image bypassed on-disk integrity validation");
		require(signatures == 1 && native_ppu_compiled == 8, "Native loader recompiled a valid generation or duplicated entries");
		ppu_module<lv2_obj> absolute;
		absolute.copy_part(info);
		absolute.parent = nullptr;
		absolute.funcs = info.funcs;
		absolute.local_bounds = info.local_bounds;
		absolute.is_relocatable = false;
		prepare_native_ppu(absolute);
		require(signatures == 2, "Native loader reused relocatable output for absolute code");
		cpu.state.store({});
		cpu.cia = 0x30000;
		cpu.ctr = 4096;
		cpu.lr = 0x40000;
		cpu.gpr[4] = 11;
		cpu.gpr[5] = 17;
		require(execute_native_ppu(cpu) && cpu.cia == 0x40000 && cpu.gpr[4] == 11 + 4096 * 3 && cpu.gpr[5] == 17 + 4096 * 7,
			"Absolute native PPU loader execution changed guest coordinates");
		cpu.state += cpu_flag::wait;
		// Constant-number SC uses LLVM's one-argument named import even though
		// AOT keeps the static decoder. Exercise actual emitted code, not a helper
		// called with an accidentally valid extra host argument.
		absolute.funcs.clear();
		ppu_function syscall;
		syscall.addr = 0x30078;
		syscall.size = 8;
		absolute.funcs.push_back(syscall);
		vm::write32(0x30078, 0x39600093); // li r11,147 (timebase frequency)
		vm::write32(0x3007c, 0x44000002); // sc
		prepare_native_ppu(absolute);
		const auto decoder = g_cfg.core.ppu_decoder.get();
		g_cfg.core.ppu_decoder.set(ppu_decoder_type::_static);
		cpu.state.store({});
		cpu.cia = 0x30078;
		cpu.gpr[3] = 0;
		cpu.gpr[11] = 0;
		require(execute_native_ppu(cpu) && cpu.gpr[3] == 80'000'000 && cpu.cia == 0x30080,
			"Native LLVM named syscall did not use the guest syscall number");
		g_cfg.core.ppu_decoder.set(decoder);
		cpu.state += cpu_flag::wait;
		cancel();
		bool rejected = false;
		try
		{
			prepare_native_ppu(info);
		}
		catch (const failure&)
		{
			rejected = true;
		}
		require(rejected, "Native loader ignored cancellation");
		begin_preparation();
		clear_game();
		require(!native_ppu_targets.target(0x30000), "Native loader retained targets after lifecycle reset");
		// Cross the existing 100 KiB / 1000-function partition boundary so this
		// exercises two actual LLVM workers, serialized signing and publication.
		active_title = "TEST00001";
		discovery_binding = std::string(64, 'd');
		ppu_module<lv2_obj> parallel;
		parallel.is_relocatable = true;
		constexpr std::uint32_t count = 1001, bytes = count * 104;
		parallel.local_bounds = {0x30000, 0x30000 + bytes};
		parallel.segs.push_back({0x30000, bytes, 1, 5, bytes, vm::base(0x30000)});
		parallel.addr_to_seg_index.emplace(0x30000, 0);
		for (std::uint32_t i = 0; i < count; ++i)
		{
			ppu_function function;
			function.addr = 0x30000 + i * 104;
			function.size = 104;
			parallel.funcs.push_back(function);
			for (std::uint32_t offset = 0; offset < 100; offset += 4)
				vm::write32(function.addr + offset, 0x60000000);
			vm::write32(function.addr + 100, 0x4e800020);
		}
		register_native_ppu_range(0x30000, 0x20000);
		const auto previous_signatures = signatures;
		prepare_native_ppu(parallel);
		require(signatures == previous_signatures + 2 && native_ppu_compiled == count && completed == count,
			"Parallel native PPU preparation lost partitions, publication or progress");
		prepare_native_ppu(parallel);
		require(signatures == previous_signatures + 2 && native_ppu_compiled == count && completed == count,
			"Parallel native PPU preparation did not reuse the signed cache");
		clear_game();
		active_title = "TEST00001";
		discovery_binding = std::string(64, 'f');
		register_native_ppu_range(0x30000, 0x20000);
		const auto before_cancel = signatures;
		cancel_signature = signatures + 2;
		rejected = false;
		try
		{
			prepare_native_ppu(parallel);
		}
		catch (const failure&)
		{
			rejected = true;
		}
		require(rejected && native_ppu_compiled == 0, "Cancelled worker preparation published partial CPU targets");
		require(g_progr_ptotal.load() == g_progr_pdone.load(), "Cancelled AOT preparation stranded native progress");
		cancel_signature = 0;
		begin_preparation();
		prepare_native_ppu(parallel);
		require(signatures == before_cancel + 3 && native_ppu_compiled == count,
			"Cancelled preparation discarded an already signed partition");
		clear_game();
		active_title = "TEST00001";
		discovery_binding = std::string(64, 'e');
		parallel.funcs.clear();
		constexpr std::uint32_t large_bytes = 0x18004;
		parallel.segs[0].size = large_bytes;
		parallel.segs[0].filesz = large_bytes;
		parallel.local_bounds = {0x30000, 0x30000 + large_bytes};
		ppu_function large;
		large.addr = 0x30000;
		large.size = large_bytes;
		parallel.funcs.push_back(large);
		for (std::uint32_t offset = 0; offset < large_bytes - 4; offset += 4)
			vm::write32(0x30000 + offset, 0x60000000);
		vm::write32(0x30000 + large_bytes - 4, 0x4e800020);
		register_native_ppu_range(0x30000, 0x20000);
		prepare_native_ppu(parallel);
		for (unsigned pass = 0; pass < 3; ++pass)
		{
			cpu.state.store({});
			cpu.cia = 0x30000;
			cpu.lr = 0x40000;
			cpu.gpr[0] = 0x12345;
			cpu.gpr[1] = 0x23456;
			cpu.gpr[2] = 0x34567;
			require(execute_native_ppu(cpu) && cpu.cia == 0x40000 && cpu.gpr[0] == 0x12345 &&
						cpu.gpr[1] == 0x23456 && cpu.gpr[2] == 0x34567,
				"Large native guard corrupted its gateway or live registers");
			cpu.state += cpu_flag::wait;
		}
		for (const auto offset : {std::uint32_t{0x8000}, large_bytes - 4})
		{
			const auto saved = vm::read32(0x30000 + offset);
			vm::write32(0x30000 + offset, saved ^ 1);
			cpu.state.store({});
			cpu.cia = 0x30000;
			require(execute_native_ppu(cpu) && cpu.cia == 0x30000 && cpu.gpr[0] == 0x12345,
				"Bounded native guard missed changed middle or tail words");
			cpu.state += cpu_flag::wait;
			vm::write32(0x30000 + offset, saved);
		}
		clear_game();
		std::cout << "Native PPU loader: signing handoff, guarded execution, parallel partitions, cache reuse, cancellation and reset passed\n";
	}
#endif

#ifdef RPCS3_AOT_HOST_TESTS
	// Optional user-owned captures exercise the production metadata decoder and
	// shared translators. No game bytes are fixtures or redistributed by tests.
	std::vector<instruction> test_native_aot_game_entries(const std::string& file, const std::string& title)
	{
		installed_game game;
		game.path = file;
		game.title_id = title;
		return analyze_game(game);
	}
	void test_native_aot_game_inventory(const std::string& file, const std::string& title)
	{
		installed_game game;
		game.path = file;
		game.title_id = title;
		manifest value;
		value.entries = analyze_game(game);
		std::size_t ppu = 0, spu = 0;
		for (const auto& entry : value.entries)
			(entry.guest_kind == 1 ? ppu : spu)++;
		std::cout << "Complete game inventory: PPU=" << ppu << " SPU=" << spu << std::endl;
		extend_inventory(value.entries, {});
		plan_modules(value);
		std::cout << "Complete game plan: modules=" << module_count(value) << " metadata_bytes=" << manifest_size(value) << std::endl;
	}
	std::vector<instruction> test_native_aot_inventory(const std::string& file)
	{
		return decode(read_text(file)).entries;
	}
	std::string test_native_aot_inventory_digest(std::span<const instruction> entries)
	{
		// Compare only raw code/origin/block identities, independent of compiler,
		// signer, publication nonce, entry ordering or generated module boundaries.
		manifest value{"BLUS00000", std::string(32, '0'), std::string(32, '0'),
			std::string(64, '0'), std::string(64, '0'), std::string(64, '0'), {}, {entries.begin(), entries.end()}};
		std::sort(value.entries.begin(), value.entries.end(), entry_less);
		hash digest;
		digest.append(encode(value));
		return digest.finish();
	}
	void test_native_aot_inventory_history(const std::string& output)
	{
		const instruction initial{1, 0x60000000, {0x60000000}};
		const instruction learned_ppu{1, 0x38600001, {0x38600001, 0x4e800020}};
		const instruction learned_spu{2, 0x40200004, {0x40200004}};
		manifest next{"BCES01143", std::string(32, '1'), std::string(32, '2'), std::string(64, '3'),
			std::string(64, '4'), std::string(64, '5'), {}, {initial}};
		manifest cached = next;
		extend_inventory(cached.entries, std::span(&learned_ppu, 1));
		cached = decode(encode(cached));
		require(compatible_inventory(cached, next), "AOT history rejected compatible code");
		extend_inventory(next.entries, cached.entries);
		extend_inventory(next.entries, std::span(&learned_spu, 1));
		require(std::equal(cached.entries.begin(), cached.entries.end(), next.entries.begin()), "AOT extension moved signed descriptor batches");
		// The next Stop contains only the newest observations. Repeated extension
		// must preserve both processors' earlier code and remove duplicate units.
		cached = decode(encode(next));
		next.entries = {initial};
		extend_inventory(next.entries, cached.entries);
		extend_inventory(next.entries, std::span(&learned_ppu, 1));
		require(next.entries.size() == 3 && std::find(next.entries.begin(), next.entries.end(), learned_spu) != next.entries.end(),
			"AOT history forgot previously compiled discoveries");
		for (auto field : {&manifest::title, &manifest::binding, &manifest::signer, &manifest::core})
		{
			auto incompatible = cached;
			(incompatible.*field)[0] ^= 1;
			require(!compatible_inventory(incompatible, next), "AOT history accepted incompatible inputs");
		}
		std::reverse(cached.entries.begin(), cached.entries.end());
		require(compatible_inventory(cached, next), "AOT history rejected append-ordered inventory");
		cached.entries.push_back(cached.entries.front());
		require(!compatible_inventory(cached, next), "AOT history accepted duplicate inventory");
		for (const std::size_t count : {511, 512, 513, 1024, 1025})
		{
			manifest prefix;
			prefix.entries.resize(count);
			prefix.translated_ppu = count;
			require(reusable_module_count(prefix) == count / module_batch_size, "AOT reuse moved a partial module boundary");
			--prefix.translated_ppu;
			require(reusable_module_count(prefix) == 0, "AOT reuse assumed declined entries were translated");
		}
		{
			manifest partial = next;
			partial.entries.assign(module_batch_size + 1, initial);
			partial.translated_ppu = module_batch_size - 1;
			partial.translated_spu = 0;
			partial.translated_modules = {{module_batch_size - 1, 0}, {0, 0}};
			partial = decode(encode(partial));
			require(reusable_module_count(partial) == 1, "Native declines discarded an unchanged closed module");
			partial.translated_modules[0].first++;
			bool rejected = false;
			try
			{
				(void)decode(encode(partial));
			}
			catch (const failure&)
			{
				rejected = true;
			}
			require(rejected, "Invalid native per-module totals accepted");
		}

		{
			manifest sized = next;
			sized.module_ends.clear();
			sized.translated_modules.clear();
			sized.translated_ppu = 0;
			sized.translated_spu = 0;
			auto costly = initial;
			costly.words.resize(128, initial.opcode);
			sized.entries.assign(1025, costly);
			plan_modules(sized);
			require(sized.module_ends.size() == 17 && module_range(sized, 0) == std::pair<std::size_t, std::size_t>{0, 64},
				"Large native units were batched by entry count alone");
			for (std::size_t part = 0; part < module_count(sized); ++part)
			{
				const auto [first, count] = module_range(sized, part);
				(void)first;
				sized.translated_modules.emplace_back(count, 0);
			}
			sized.translated_ppu = sized.entries.size();
			sized = decode(encode(sized));
			require(reusable_module_count(sized) == 17, "Explicit partial boundary was not reusable");
			manifest extended;
			extended.entries = sized.entries;
			extended.entries.push_back(learned_ppu);
			extended.module_ends = sized.module_ends;
			plan_modules(extended);
			require(extended.module_ends.size() == 18 && extended.module_ends[16] == 1025 && extended.module_ends.back() == 1026,
				"Incremental plan moved an immutable signed boundary");
			std::cout << "Native AOT partitions: bounded guest-word cost, legacy decoding and immutable partial-boundary reuse passed\n";
		}

		// A compiler update recompiles validated guest observations, while native
		// binaries keep their exact core binding. Exercise real input hashing too.
		const auto fixture = path::path(output) / "observation-inputs";
		path::create_directories(fixture);
		const auto input = fixture / "EBOOT.BIN";
		write_text(input, "native observation input fixture");
		installed_game game;
		game.title_id = next.title;
		game.version = "01.00";
		game.path = fixture.string();
		const auto update_dir = path::path(fs::get_config_dir()) / "dev_hdd0/game" / game.title_id;
		path::create_directories(update_dir);
		const auto metadata_file = update_dir / "PARAM.SFO";
		const bool had_metadata = path::exists(metadata_file);
		const auto saved_metadata = had_metadata ? read_text(metadata_file) : std::string{};
		struct restore_metadata
		{
			path::path file;
			bool existed;
			std::string bytes;
			~restore_metadata()
			{
				if (existed)
					write_text(file, bytes);
				else
					path::remove(file);
			}
		} metadata_restore{metadata_file, had_metadata, saved_metadata};
		const auto original_binding = input_binding(game, false);
		write_text(metadata_file, "changed update selection metadata");
		require(input_binding(game, false) != original_binding, "Changed update metadata accepted cached code");
		write_text(metadata_file, "different update selection metadata");
		const auto changed_binding = input_binding(game, false);
		path::remove(metadata_file);
		require(input_binding(game, false) != changed_binding, "Removed update metadata accepted cached code");
		if (had_metadata)
			write_text(metadata_file, saved_metadata);
		std::cout << "AOT input binding: update metadata changes and removal invalidate compiled code\n";
		const auto old_core = std::string(64, 'a'), new_core = std::string(64, 'b');
		cached = next;
		cached.core = old_core;
		cached.binding = binding(game, false, old_core);
		next.core = new_core;
		next.binding = binding(game, false, new_core);
		require(cached.binding != next.binding && !compatible_inventory(cached, next), "Core update accepted old native code");
		require(compatible_observations(cached, next, binding(game, false, cached.core)), "Core update lost raw observations");
		std::vector<instruction> restored;
		collect_observations(cached, restored);
		require(std::find(restored.begin(), restored.end(), learned_spu) != restored.end(), "Core update lost learned SPU words");
		for (auto field : {&manifest::title, &manifest::signer, &manifest::binding})
		{
			auto changed = cached;
			(changed.*field)[0] ^= 1;
			require(!compatible_observations(changed, next, cached.binding), "Observation identity mismatch accepted");
		}
		write_text(input, "modified executable input fixture");
		require(!compatible_observations(cached, next, binding(game, false, old_core)), "Changed source accepted old observations");
		write_text(input, "native observation input fixture");
		game.version = "01.01";
		require(!compatible_observations(cached, next, binding(game, false, old_core)), "Changed game version accepted observations");
		game.version = "01.00";
		const auto config_file = path::path(fs::get_config_dir()) / "config.yml";
		const auto saved_config = read_text(config_file);
		struct restore_config
		{
			path::path file;
			std::string bytes;
			~restore_config()
			{
				write_text(file, bytes);
			}
		} restore{config_file, saved_config};
		cfg_root changed_config;
		require(changed_config.from_string(saved_config), "Cannot read test config");
		changed_config.core.ppu_debug.set(!changed_config.core.ppu_debug.get());
		write_text(config_file, changed_config.to_string());
		require(!compatible_observations(cached, next, binding(game, false, old_core)), "Changed effective settings accepted observations");
		write_text(config_file, saved_config);
		changed_config.from_string(saved_config);
		changed_config.audio.volume.set(changed_config.audio.volume.get() == 50 ? 60 : 50);
		write_text(config_file, changed_config.to_string());
		require(compatible_observations(cached, next, binding(game, false, old_core)), "Audio-only settings invalidated CPU code");
		write_text(config_file, saved_config);
		const auto relocated = path::path(output) / "relocated-inputs";
		path::create_directories(relocated);
		path::copy_file(input, relocated / "EBOOT.BIN", path::copy_options::overwrite_existing);
		game.path = relocated.string();
		require(compatible_observations(cached, next, binding(game, false, old_core)), "Container relocation invalidated identical CPU inputs");
		game.path = fixture.string();
		path::remove_all(relocated);
		const auto patch = path::path(fs::get_config_dir()) / "patch_config.yml";
		const bool had_patch = path::exists(patch);
		const auto saved_patch = had_patch ? read_text(patch) : std::string{};
		write_text(patch, "changed test patch inputs");
		const bool rejected_patch = !compatible_observations(cached, next, binding(game, false, old_core));
		if (had_patch)
			write_text(patch, saved_patch);
		else
			path::remove(patch);
		require(rejected_patch && compatible_observations(cached, next, binding(game, false, old_core)), "Patch binding or restoration failed");
		manifest boundaries = cached;
		boundaries.entries = {{1, 0x4e800020, {0x4e800020, 0x60000000}}};
		restored.clear();
		collect_observations(boundaries, restored);
		require(restored.size() == 2 && std::all_of(restored.begin(), restored.end(), [](const auto& e)
											{
												return e.words.size() == 1;
											}),
			"Old observations bypassed current branch boundaries");
		{
			const auto saved_root = root, saved_signer = signer, saved_core = core_digest;
			configure((path::path(output) / "cancel-admission").string(), std::string(64, 'c'), new_core);
			begin_preparation();
			cancel();
			bool rejected = false;
			try
			{
				(void)prepare(game);
			}
			catch (const failure& error)
			{
				rejected = error.message.find("cancelled") != std::string::npos;
			}
			require(rejected, "Preparation erased cancellation delivered before C entry");
			begin_preparation();
			checkpoint();
			root = saved_root;
			signer = saved_signer;
			core_digest = saved_core;
			std::cout << "AOT cancellation admission: queued cancellation retained and next job reset explicitly\n";
		}
	}
	void test_native_aot_admission(const std::string& output, const std::string& library, std::span<const instruction> entries)
	{
		const auto saved_root = root, saved_signer = signer, saved_core = core_digest;
		configure((path::path(output) / "admission").string(), std::string(64, 'c'), std::string(64, 'b'));
		installed_game game;
		game.title_id = "TEST00001";
		game.version = "01.00";
		game.path = (path::path(output) / "admission-input").string();
		write_text(path::path(game.path) / "EBOOT.BIN", "admission source");
		const auto directory = title_root(game.title_id);
		manifest value{game.title_id, nonce(), process_nonce, binding(game, false), signer, core_digest, {}, {entries.begin(), entries.end()}};
		const auto artifact = directory / value.generation;
		path::create_directory(artifact);
		path::copy_file(library, artifact / "signed-0.dylib");
		auto* handle = dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
		require(handle != nullptr, "Cannot inspect admission fixture");
		const auto descriptor = reinterpret_cast<const rpcs3_aot_module_v2* (*)()>(dlsym(handle, "rpcs3_aot_module_v2"))();
		for (std::uint32_t i = 0; i < descriptor->entry_count; ++i)
			if (descriptor->entries[i].invoke)
				(descriptor->entries[i].guest_kind == 1 ? value.translated_ppu : value.translated_spu)++;
		value.module_ends = {static_cast<std::uint32_t>(value.entries.size())};
		value.translated_modules = {{value.translated_ppu, value.translated_spu}};
		const auto publish = [&](const manifest& selected, const std::string& bytes)
		{
			const auto generation = directory / selected.generation;
			path::create_directories(generation);
			write_text(generation / "signed-0.dylib", bytes);
			const auto digests = hash_file(generation / "signed-0.dylib") + "\n";
			auto record = selected;
			hash aggregate;
			aggregate.append(digests);
			record.signed_digest = aggregate.finish();
			write_text(generation / "digests.txt", digests);
			write_text(generation / "manifest.bin", encode(record));
		};
		publish(value, read_text(library, 8 * 1024 * 1024));
		write_text(directory / "current", value.generation);
		begin_game(game);
		require(stage == 6 && statistics().compiled_ppu == value.translated_ppu && statistics().compiled_spu == value.translated_spu,
			"Valid execution admission failed");
		require(!path::exists(directory / "admission.pending"), "Successful admission left a crash marker");
		finish_game();
		clear_game();
		begin_preparation();
		require(prepare(game).empty(), "Unchanged Stop/start recompiled prepared code");
		begin_game(game);
		const instruction additional{1, 0x38600073, {0x38600073, 0x4e800020}, 0x70000, 0, {{0, 2}}};
		restore_discoveries(std::span(&additional, 1));
		finish_game();
		clear_game();
		begin_preparation();
		const auto extension = prepare(game);
		require(!extension.empty() && signing_count(extension) == 1, "New discovery did not produce exactly one incremental signing job");
		const auto& extended = signing_jobs.at(extension);
		require(total == 1 && completed == 1, "Incremental progress counted reused code as new compilation");
		require(extended.next_module == 1 && extended.value.module_ends.front() == value.entries.size() &&
					hash_file(directory / extension / "signed-0.dylib") == hash_file(artifact / "signed-0.dylib"),
			"Stop/start extension recompiled or moved an existing signed module");
		signing_jobs.erase(extension);
		path::remove_all(directory / extension);
		std::cout << "Native AOT Stop/start: unchanged generation reused; only new discoveries compiled and signed\n";

		const auto handles = loaded_modules.size();
		begin_game(game);
		clear_game();
		require(loaded_modules.size() == handles, "Repeated boot duplicated loaded module state");
		auto identical = value;
		identical.generation = nonce();
		publish(identical, read_text(library, 8 * 1024 * 1024));
		write_text(directory / "current", identical.generation);
		begin_game(game);
		clear_game();
		require(stage == 6 && loaded_modules.size() == handles, "Identical signed image reopened across generations");
		write_text(directory / "current", value.generation);
		{
			auto incremental = value;
			incremental.generation = nonce();
			incremental.entries.insert(incremental.entries.end(), entries.begin(), entries.end());
			incremental.module_ends = {static_cast<std::uint32_t>(entries.size()), static_cast<std::uint32_t>(entries.size() * 2)};
			const auto pending = directory / incremental.generation;
			write_text(pending / "unsigned-1.dylib", read_text(library, 8 * 1024 * 1024));
			signing_jobs.emplace(incremental.generation, signing_job{incremental, 1, {hash_file(artifact / "signed-0.dylib")}});
			require(signing_count(incremental.generation) == 1 && path::path(signing_paths(incremental.generation).first).filename() == "unsigned-1.dylib",
				"Signing handoff includes immutable reused images");
			signing_jobs.erase(incremental.generation);
			path::remove_all(pending);
		}

		// An interrupted candidate and an ordinary dyld failure both preserve and
		// admit the same compatible rollback generation, without retrying bad code.
		write_text(directory / "previous", value.generation);
		auto bad = value;
		bad.generation = nonce();
		publish(bad, "invalid Mach-O image");
		write_text(directory / "current", bad.generation);
		write_text(directory / "admission.pending", bad.generation);
		begin_game(game);
		require(stage == 6 && path::exists(directory / bad.generation / "rejected.txt"), "Interrupted admission did not quarantine or recover");
		clear_game();
		bad.generation = nonce();
		publish(bad, "invalid Mach-O image");
		write_text(directory / "current", bad.generation);
		begin_game(game);
		require(stage == 6 && read_text(directory / bad.generation / "rejected.txt").find("OS rejected") != std::string::npos,
			"Loader failure lost its reason or compatible rollback");
		clear_game();
		write_text(directory / "admitted", bad.generation);
		write_text(directory / "previous", value.generation);
		require(rollback_generation(value, directory) == value.generation, "Rejected current/admitted code displaced a valid rollback");
		write_text(directory / "admitted", bad.generation);
		write_text(directory / "previous", bad.generation);
		begin_game(game);
		require(stage == 8 && statistics().compiled_ppu == 0 && statistics().compiled_spu == 0,
			"Rejected code remained active or reported as prepared");
		clear_game();
		// Native pending-file rejection cannot replace the last valid pointer.
		const auto before = read_text(directory / "current");
		bool rejected = false;
		try
		{
			write_text(directory, "cannot overwrite a directory");
		}
		catch (...)
		{
			rejected = true;
		}
		require(rejected && read_text(directory / "current") == before, "Failed write damaged the current pointer");
		const auto abandoned = directory / nonce();
		write_text(abandoned / "module-0.o", "abandoned object");
		write_text(artifact / "module-0.o", "published object");
		write_text(artifact / "unsigned-0.dylib", "published unsigned copy");
		write_text(artifact / "signed-99.dylib", "unmapped copied image");
		write_text(directory / "keep-me" / "unowned.txt", "unrelated diagnostic");
		write_text(directory / "current", value.generation);
		write_text(directory / "previous", value.generation);
		collect_garbage(game.title_id);
		require(!path::exists(abandoned) && !path::exists(directory / identical.generation) && !path::exists(artifact / "module-0.o") &&
					!path::exists(artifact / "unsigned-0.dylib") && path::exists(artifact / "signed-0.dylib") &&
					path::exists(directory / "keep-me/unowned.txt"),
			"AOT collection lost loaded code or unrelated data");
		require(path::exists(artifact / "signed-99.dylib"), "Active generation copy was removed prematurely");
		const auto before_clear = cache_size(game.title_id);
		const auto removed = clear_cache(game.title_id);
		require(removed > 0 && removed <= before_clear && path::exists(artifact / "signed-0.dylib") &&
					!path::exists(artifact / "signed-99.dylib") && !path::exists(directory / "current") &&
					path::exists(directory / "clear.pending"),
			"Cache clear deleted mapped code, retained an unmapped copy or failed to defer it");
		root = saved_root;
		signer = saved_signer;
		core_digest = saved_core;
		std::cout << "Native AOT storage: abandoned/intermediate collection, cache byte accounting, mapped-image retention and deferred clear passed\n";
		std::cout << "Native AOT admission: valid/repeated load, interrupted/failed image quarantine, compatible rollback, explicit fallback and failed publication passed\n";
	}
	void test_native_aot_digest_table(const std::string& output)
	{
		// Compare the system backend with the original portable implementation,
		// including empty input, SHA block boundaries and file-read boundaries.
		for (const std::size_t size : {0, 1, 55, 56, 63, 64, 65, 65535, 65536, 65537})
		{
			std::vector<unsigned char> bytes(size);
			for (std::size_t i = 0; i < size; ++i)
				bytes[i] = static_cast<unsigned char>(i * 37);
			unsigned char reference[32];
			require(!mbedtls_sha256_ret(bytes.data(), size, reference, 0), "Reference SHA-256 failed");
			std::ostringstream expected;
			for (auto byte : reference)
				expected << std::hex << std::setfill('0') << std::setw(2) << unsigned(byte);
			hash actual;
			const auto split = size / 3;
			actual.append(bytes.data(), split);
			auto copied = actual;
			if (size)
				copied.append(bytes.data() + split, size - split);
			require(copied.finish() == expected.str(), "Copied SHA-256 prefix changed the artifact identity");
			if (size)
				actual.append(bytes.data() + split, size - split);
			require(actual.finish() == expected.str(), "Accelerated SHA-256 changed the artifact identity");
		}

		const path::path directory = path::path(output) / "digest-table";
		path::create_directories(directory);
		const std::string content = "native AOT digest fixture";
		hash file;
		file.append(content.data(), content.size());
		const auto digest = file.finish();
		// Synthetic digest inputs need no publication transaction. Transaction and
		// directory durability behavior is exercised by the admission fixtures.
		for (std::size_t i = 0; i < maximum_modules; ++i)
		{
			fs::file fixture((directory / module_name("signed", i, ".dylib")).string(), fs::rewrite);
			require(fixture && fixture.write(content.data(), content.size()) == content.size(), "Cannot create digest fixture");
		}
		manifest value;
		for (const std::size_t count : {64, 65, 1126, 2048})
		{
			value.entries.resize(count * module_batch_size);
			std::string table;
			for (std::size_t i = 0; i < count; ++i)
				table += digest + "\n";
			hash aggregate;
			aggregate.append(table);
			value.signed_digest = aggregate.finish();
			write_text(directory / "digests.txt", table);
			require(module_digests(value, directory).size() == count, "AOT digest reader dropped modules");
		}
		write_text(directory / module_name("signed", 2047, ".dylib"), "changed");
		bool rejected = false;
		try
		{
			(void)module_digests(value, directory);
		}
		catch (const failure& error)
		{
			rejected = error.message == "Prepared AOT library is damaged";
		}
		require(rejected, "AOT digest reader accepted changed code");
		write_text(directory / "digests.txt", std::string(65 * maximum_modules + 1, 'a'));
		rejected = false;
		try
		{
			(void)module_digests(value, directory);
		}
		catch (const failure& error)
		{
			rejected = error.message == "Invalid AOT metadata file";
		}
		require(rejected, "AOT digest reader accepted an oversized table");
	}
#endif

} // namespace rpcs3::ios::aot
