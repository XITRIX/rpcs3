#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>
#include "reversal.h"

namespace
{
constexpr unsigned count = 1024;
using bytes = std::array<std::uint8_t, 16>;
alignas(16) std::array<std::uint8_t, count * 16 + 32> a, b, c, actual, expected;
std::uint64_t seed = 0x789320ab51cde42full;
std::uint8_t random_byte()
{
	seed ^= seed << 13;
	seed ^= seed >> 7;
	seed ^= seed << 17;
	return seed;
}
bytes reference(const test& t, const bytes& x, const bytes& y, const bytes& z)
{
	bytes result{};
	for (unsigned i = 0; i < 16; ++i)
	{
		switch (t.kind)
		{
		case 0:
		{
			const auto slot = z[12] & (16 / t.size - 1);
			const auto preferred = t.size == 8 ? 8 : 12;
			result[i] = (15 - i) / t.size == slot ? y[preferred + (15 - i) % t.size] : x[i];
			break;
		}
		case 1: result[i] = y[i] < 16 ? x[15 - y[i]] : 0; break;
		case 2: result[i] = x[(15 - i + y[12]) & 15]; break;
		case 3: result[i] = x[i] & y[i]; break;
		case 4: result[i] = x[i] | y[i]; break;
		case 5: result[i] = x[i] ^ y[i]; break;
		case 6: result[i] = (x[i] & z[15 - i]) | (y[i] & ~z[15 - i]); break;
		case 7: result[i] = z[0] ? x[i] : y[i]; break;
		case 8: result[i] = x[i] ^ y[i] ^ x[15 - i] ^ y[15 - i]; break;
		case 9: result[i] = z[15 - i] ? x[i] : y[i]; break;
		case 10: result[i] = x[15 - i]; break;
		case 11:
		{
			const unsigned selector = z[i];
			if (selector & 128)
				result[i] = selector < 192 ? 0 : selector < 224 ? 255 : 128;
			else
			{
				const unsigned side = (t.size >> 8) & 1;
				const unsigned index = (selector ^ (t.size & 512 ? 0 : 15)) & 15;
				result[i] = ((selector >> 4) & 1) == side ? t.size & 255 : side ? x[index] : y[index];
			}
			break;
		}
		}
	}
	return result;
}
void make_expected(const test& t, unsigned offset)
{
	bytes state{};
	for (unsigned i = 0; i < count; ++i)
	{
		bytes x, y, z;
		std::memcpy(x.data(), a.data() + offset + i * 16, 16);
		std::memcpy(y.data(), b.data() + offset + i * 16, 16);
		std::memcpy(z.data(), c.data() + offset + i * 16, 16);
		if (t.chain)
		{
			auto& dependent = t.chain == 1 ? x : t.chain == 2 ? y : z;
			for (unsigned j = 0; j < 16; ++j)
				dependent[j] ^= state[j];
		}
		state = reference(t, x, y, z);
		std::memcpy(expected.data() + offset + i * 16, state.data(), 16);
	}
}
}
int main(int argc, char** argv)
{
	const bool benchmark = argc == 2 && std::string_view(argv[1]) == "--bench";
	for (const auto& t : tests)
	{
		for (unsigned pass = 0; pass < 320; ++pass)
		{
			for (auto* input : {&a, &b, &c})
				for (auto& byte : *input)
					byte = random_byte();
			const auto offset = pass & 15;
			// Enumerate every possible table selector and bounded insert
			// index, including TBL's high-bit/out-of-range zero behavior.
			if (pass < 256)
			{
				std::fill(b.begin(), b.end(), pass);
				std::fill(c.begin(), c.end(), pass);
			}
			make_expected(t, offset);
			for (unsigned version = 0; version < 2; ++version)
			{
				actual.fill(0xa5);
				t.functions[version](a.data() + offset, b.data() + offset, c.data() + offset, actual.data() + offset, count);
				if (std::memcmp(expected.data() + offset, actual.data() + offset, count * 16) ||
					!std::all_of(actual.begin(), actual.begin() + offset, [](auto x) { return x == 0xa5; }) ||
					!std::all_of(actual.begin() + offset + count * 16, actual.end(), [](auto x) { return x == 0xa5; }))
				{
					std::fprintf(stderr, "FAIL %s %s pass %u offset %u\n", t.name, version ? "candidate" : "baseline", pass, offset);
					return 1;
				}
			}
		}
		std::printf("PASS %s scalar oracle: 327680 vectors per version\n", t.name);
	}
	if (!benchmark)
		return 0;
	for (const auto& t : tests)
	{
		if ((t.kind >= 7 && t.kind != 11) || (t.kind == 11 && (t.size & 255) != 37))
			continue;
		std::array<std::vector<double>, 2> samples;
		for (unsigned round = 0; round < 14; ++round)
			for (unsigned order = 0; order < 2; ++order)
			{
				const auto version = order ^ (round & 1);
				const auto start = std::chrono::steady_clock::now();
				for (unsigned repeat = 0; repeat < 2048; ++repeat)
					t.functions[version](a.data(), b.data(), c.data(), actual.data(), count);
				const auto ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / (count * 2048.0);
				if (round >= 2)
					samples[version].push_back(ns);
			}
		double median[2];
		std::printf("BENCH %s ", t.name);
		for (unsigned v = 0; v < 2; ++v)
		{
			auto& times = samples[v];
			std::sort(times.begin(), times.end());
			median[v] = (times[5] + times[6]) / 2;
			std::printf("%s %.4f [%.4f,%.4f] ", v ? "new" : "old", median[v], times.front(), times.back());
		}
		std::printf("improvement %.2f%%\n", 100 * (1 - median[1] / median[0]));
		std::fflush(stdout);
	}
}
