#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>
#include "compare.h"

namespace
{
constexpr unsigned count = 1024;
using bytes = std::array<std::uint8_t, 16>;
using buffer = std::array<std::uint8_t, count * 16 + 32>;
alignas(16) buffer a, b, c, actual, expected;
std::uint64_t seed = 0x49de271583bad7bfull;
std::uint32_t random32()
{
	seed ^= seed << 13;
	seed ^= seed >> 7;
	seed ^= seed << 17;
	return static_cast<std::uint32_t>(seed);
}
std::uint32_t read_be(const std::uint8_t* p, unsigned size)
{
	std::uint32_t result = 0;
	for (unsigned i = 0; i < size; ++i)
		result = (result << 8) | p[i];
	return result;
}
void write_be(std::uint8_t* p, unsigned size, std::uint32_t value)
{
	for (unsigned i = 0; i < size; ++i)
		p[size - 1 - i] = static_cast<std::uint8_t>(value >> (8 * i));
}
bytes reference(const test& t, bytes x, bytes y)
{
	const auto original = x;
	if (t.plain)
	{
		std::reverse(x.begin(), x.end());
		std::reverse(y.begin(), y.end());
	}
	bytes result{}, predicate{}, sum_bytes{};
	const unsigned size = t.bits / 8;
	const std::uint32_t mask = t.bits == 32 ? UINT32_MAX : 65535;
	const std::uint32_t sign = 1u << (t.bits - 1);
	for (unsigned i = 0; i < 16; i += size)
	{
		const auto lhs = read_be(x.data() + i, size);
		const auto rhs = t.immediate ? t.constants[(16 - i) / size - 1] : read_be(y.data() + i, size);
		const auto sum = (lhs + rhs) & mask;
		bool condition = false;
		switch (t.kind)
		{
		case 0: case 6: condition = (lhs ^ sign) > (rhs ^ sign); break;
		case 1: condition = lhs > rhs; break;
		case 2: condition = lhs <= rhs; break;
		case 3: condition = std::uint64_t(lhs) + rhs > mask; break;
		case 4: break;
		case 5: condition = lhs == rhs; break;
		default: std::abort();
		}
		const auto value = t.kind == 4 ? sum : condition ? ((t.kind == 2 || t.kind == 3 || t.kind == 6) ? 1u : mask) : 0u;
		write_be(result.data() + i, size, value);
		write_be(predicate.data() + i, size, condition);
		write_be(sum_bytes.data() + i, size, sum);
	}
	const auto base = result;
	for (unsigned i = 0; i < 16; ++i)
	{
		if (t.shared == 1 || t.shared == 2) result[i] ^= original[15 - i];
		if (t.shared == 3) result[i] ^= predicate[15 - i];
		if (t.shared == 4 || t.shared == 5) result[i] ^= base[15 - i];
		if (t.shared == 6) result[i] ^= sum_bytes[15 - i];
	}
	return result;
}
void oracle(const test& t, unsigned offset)
{
	bytes state{};
	for (unsigned j = 0; j < count; ++j)
	{
		bytes x, y;
		std::memcpy(x.data(), a.data() + offset + j * 16, 16);
		std::memcpy(y.data(), b.data() + offset + j * 16, 16);
		if (t.chain)
			for (unsigned k = 0; k < 16; ++k)
				(t.chain == 1 ? x : y)[k] ^= state[k];
		state = reference(t, x, y);
		std::memcpy(expected.data() + offset + j * 16, state.data(), 16);
	}
}
}
int main(int argc, char** argv)
{
	const bool bench = argc == 2 && std::string_view(argv[1]) == "--bench";
	constexpr unsigned passes = 256;
	for (const auto& t : tests)
	{
		for (unsigned pass = 0; pass < passes; ++pass)
		{
			for (auto* input : {&a, &b, &c})
				for (auto& byte : *input) byte = static_cast<std::uint8_t>(random32());
			const unsigned offset = pass & 15, size = t.bits / 8;
			const std::uint32_t mask = t.bits == 32 ? UINT32_MAX : 65535;
			const std::uint32_t sign = 1u << (t.bits - 1);
			const std::array<std::uint32_t, 16> edges{0, 1, 2, 37, 127, 128, 255, 256, sign - 1, sign, sign + 1, mask - 1, mask, 0x1234, mask - 255, mask - 511};
			for (unsigned j = 0; j < count * 16; j += size)
			{
				std::uint32_t lhs = random32() & mask, rhs = random32() & mask;
				if (pass < 128)
				{
					lhs = t.bits == 16 ? ((pass / 16) * (count * 16 / size) + j / size) & mask : edges[(j / size + pass / 16) % edges.size()];
					rhs = edges[pass % edges.size()];
				}
				else if (pass < 192)
				{
					if (t.immediate) lhs = t.constants[(j / size) % (16 / size)] + pass % 3 - 1;
					rhs = pass % 4 == 0 ? lhs : pass % 4 == 1 ? lhs + 1 : pass % 4 == 2 ? lhs - 1 : mask - lhs;
				}
				write_be(a.data() + offset + j, size, lhs);
				write_be(b.data() + offset + j, size, rhs);
			}
			oracle(t, offset);
			for (unsigned version = 0; version < 2; ++version)
			{
				actual.fill(0xa5);
				t.functions[version](a.data() + offset, b.data() + offset, c.data() + offset, actual.data() + offset, count);
				if (std::memcmp(expected.data() + offset, actual.data() + offset, count * 16) ||
					!std::all_of(actual.begin(), actual.begin() + offset, [](auto v) { return v == 0xa5; }) ||
					!std::all_of(actual.begin() + offset + count * 16, actual.end(), [](auto v) { return v == 0xa5; }))
				{
					std::fprintf(stderr, "FAIL %s %s pass %u offset %u\n", t.name, version ? "candidate" : "baseline", pass, offset);
					return 1;
				}
			}
		}
		std::printf("PASS %s scalar oracle: %u vectors per version\n", t.name, passes * count);
	}
	if (!bench) return 0;
	const char* repeat_env = std::getenv("SPU_COMPARE_BENCH_REPETITIONS");
	const unsigned repetitions = repeat_env ? std::max(1, std::atoi(repeat_env)) : 16384;
	for (const auto& t : tests)
	{
		if (std::string_view(t.name).starts_with("guard_") || (t.immediate && t.constants[0] != 37)) continue;
		std::array<std::vector<double>, 2> samples;
		for (unsigned round = 0; round < 14; ++round)
			for (unsigned order = 0; order < 2; ++order)
			{
				const unsigned version = order ^ (round & 1);
				const auto start = std::chrono::steady_clock::now();
				for (unsigned repeat = 0; repeat < repetitions; ++repeat)
					t.functions[version](a.data(), b.data(), c.data(), actual.data(), count);
				const auto ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / (count * double(repetitions));
				if (round >= 2) samples[version].push_back(ns);
			}
		double median[2];
		std::printf("BENCH %s ", t.name);
		for (unsigned v = 0; v < 2; ++v)
		{
			auto& s = samples[v]; std::sort(s.begin(), s.end());
			median[v] = (s[5] + s[6]) / 2;
			std::printf("%s %.4f [%.4f,%.4f] ", v ? "new" : "old", median[v], s.front(), s.back());
		}
		std::printf("improvement %.2f%%\n", 100 * (1 - median[1] / median[0]));
		std::fflush(stdout);
	}
}
