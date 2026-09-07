#include "Utilities/JITProfile.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <thread>
#include <unistd.h>

namespace
{
	struct record
	{
		std::uintptr_t address;
		std::size_t size, offset, captured;
		std::uint64_t timestamp;
		std::string name;
	};

	std::vector<record> read_map(const std::string& stem)
	{
		std::ifstream stream(stem + ".map");
		std::vector<record> records;
		std::string line;
		while (std::getline(stream, line))
		{
			record r{};
			std::istringstream fields(line);
			assert(fields >> std::hex >> r.address >> r.size >> r.offset >> r.captured >> std::dec >> r.timestamp >> r.name);
			std::string extra;
			assert(!(fields >> extra));
			records.push_back(r);
		}
		return records;
	}

	std::string read_code(const std::string& stem)
	{
		std::ifstream stream(stem + ".bin", std::ios::binary);
		return {std::istreambuf_iterator<char>{stream}, {}};
	}
}

int main()
{
	char directory[] = "/tmp/rpcs3-jit-profile-XXXXXX";
	assert(mkdtemp(directory));
	const std::string root = std::string{directory} + "/";
	const std::string payload = "0123456789abcdef";
	const auto address = reinterpret_cast<std::uintptr_t>(payload.data());

	{
		jit_profile::writer output;
		assert(!output.enabled());
		output.append(1, 16, "disabled-invalid-address");
		output.flush();
		assert(!output.open(root + "absent/failed"));
		assert(!output.enabled());
		assert(output.open(root + "normal"));
		assert(!output.open(root + "second"));
		assert(!std::filesystem::exists(root + "second.map"));

		jit_profile::pending_batch batch;
		batch.add(1, 16, "failed-finalization-invalid-address");
		batch.complete(output, false);
		batch.complete(output, true);
		assert(read_map(root + "normal").empty());
		batch.add(address, payload.size(), "__spu-test\nwith\tspaces");
		assert(read_map(root + "normal").empty()); // Nothing before finalization.
		batch.complete(output, true);
		batch.complete(output, true); // No duplicate publication.
		const auto records = read_map(root + "normal");
		assert(records.size() == 1);
		assert(records[0].address == address && records[0].size == payload.size());
		assert(records[0].offset == 0 && records[0].captured == payload.size());
		assert(records[0].name == "__spu-test_with_spaces");
		assert(read_code(root + "normal") == payload);
	}
	{
		jit_profile::writer output;
		assert(!output.open(root + "normal")); // Exclusive creation preserves earlier evidence.
		assert(read_code(root + "normal") == payload);
	}
	{
		jit_profile::writer output({3, 4096, 17, 16});
		assert(output.open(root + "limited"));
		output.append(address, 16, std::string(1000, 'x'));
		output.append(1, 16, "code-budget-exhausted"); // Metadata only, no invalid read.
		output.append(address, 1, "last-byte");
		output.append(1, 1, "record-limit");
		output.flush();
		assert(!output.enabled());
		const auto records = read_map(root + "limited");
		assert(records.size() == 3 && records[0].name.size() == 256);
		assert(records[1].offset == 16 && records[1].captured == 0);
		assert(records[2].offset == 16 && records[2].captured == 1);
		assert(read_code(root + "limited") == payload + "0");
	}
	{
		jit_profile::writer output;
		assert(output.open(root + "arena", address, payload.size()));
		output.append(address - 1, 1, "before-arena");
		output.append(address + payload.size(), 1, "after-arena");
		output.append(address, payload.size() + 1, "crossing-arena-end");
		output.append(address, std::numeric_limits<std::size_t>::max(), "overflowing-size");
		output.append(address, payload.size(), "whole-arena");
		output.flush();
		assert(read_map(root + "arena").size() == 1);
		assert(read_code(root + "arena") == payload);
	}
	{
		jit_profile::writer output({10, 1, 1024, 1024});
		assert(output.open(root + "map-limit"));
		output.append(1, 16, "too-large"); // Map budget is checked before any code read.
		output.flush();
		assert(!output.enabled());
		assert(read_map(root + "map-limit").empty());
		assert(read_code(root + "map-limit").empty());
	}
	{
		jit_profile::writer output({10000, 1024 * 1024, 0, 0});
		assert(output.open(root + "batch-limit"));
		jit_profile::pending_batch batch;
		for (unsigned i = 0; i < 5000; i++) batch.add(1, 16, "metadata");
		batch.complete(output, true);
		assert(read_map(root + "batch-limit").size() == 4096);
		assert(read_code(root + "batch-limit").empty());
	}
	{
		jit_profile::writer output;
		assert(output.open(root + "parallel"));
		std::vector<std::thread> threads;
		for (unsigned thread = 0; thread < 8; thread++)
		{
			threads.emplace_back([&]
			{
				jit_profile::pending_batch batch;
				for (unsigned i = 0; i < 512; i++) batch.add(address, payload.size(), "__spu-parallel");
				batch.complete(output, true);
			});
		}
		for (auto& thread : threads) thread.join();
		output.flush();
		const auto records = read_map(root + "parallel");
		const auto code = read_code(root + "parallel");
		assert(records.size() == 4096 && code.size() == records.size() * payload.size());
		std::uint64_t previous_time = 0;
		for (std::size_t i = 0; i < records.size(); i++)
		{
			const auto& r = records[i];
			assert(r.address == address && r.offset == i * payload.size());
			assert(r.captured == payload.size() && code.substr(r.offset, r.captured) == payload);
			assert(r.timestamp >= previous_time);
			previous_time = r.timestamp;
		}
	}
	std::filesystem::remove_all(directory);
	std::puts("JIT profile: disabled/failure/finalization/budget/concurrent snapshot tests passed");
}
