#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

extern "C" void signed_baseline(const uint32_t*, uint32_t*, unsigned);
extern "C" void signed_candidate(const uint32_t*, uint32_t*, unsigned);
extern "C" void unsigned_baseline(const uint32_t*, uint32_t*, unsigned);
extern "C" void unsigned_candidate(const uint32_t*, uint32_t*, unsigned);
using conversion = void (*)(const uint32_t*, uint32_t*, unsigned);
volatile uint32_t sink;

// Decode exponent/significand with integer arithmetic, independently of both
// LLVM lowering expressions and the host's floating-point conversion behavior.
uint32_t reference(uint32_t bits, bool is_signed)
{
	const bool negative = bits >> 31;
	const unsigned exponent = (bits >> 23) & 255;
	if (!is_signed && negative)
		return 0;
	if (exponent >= unsigned(is_signed ? 158 : 159))
		return is_signed ? (negative ? 0x80000000u : 0x7fffffffu) : 0xffffffffu;
	if (exponent < 127)
		return 0;
	uint32_t magnitude = (bits & 0x7fffff) | 0x800000;
	const int shift = int(exponent) - 150;
	magnitude = shift >= 0 ? magnitude << shift : magnitude >> -shift;
	return negative ? 0u - magnitude : magnitude;
}

double measure(conversion function)
{
	alignas(128) uint32_t input[4096], output[4096];
	for (unsigned i = 0; i < 4096; i++)
		input[i] = i * 0x9e3779b1u;
	const auto start = std::chrono::steady_clock::now();
	for (unsigned i = 0; i < 10000; i++)
		function(input, output, 4096);
	sink = output[128];
	return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / (10000 * 1024);
}

int main(int argc, char**)
{
	const uint64_t limit = argc > 1 ? (1ull << 32) : (1ull << 24);
	const auto start = std::chrono::steady_clock::now();
	alignas(128) uint32_t input[4096], actual[4096], baseline[4096];
	for (uint64_t base = 0; base < limit; base += 4096)
	{
		// Odd multiplication permutes all 2^32 inputs; the shorter mode samples
		// the entire exponent range rather than only small bit encodings.
		for (unsigned j = 0; j < 4096; j++)
			input[j] = uint32_t(base + j) * 0x9e3779b1u;
		for (bool is_signed : {false, true})
		{
			(is_signed ? signed_candidate : unsigned_candidate)(input, actual, 4096);
			(is_signed ? signed_baseline : unsigned_baseline)(input, baseline, 4096);
			for (unsigned j = 0; j < 4096; j++)
			{
				const auto expected = reference(input[j], is_signed);
				if (actual[j] != expected || actual[j] != baseline[j])
				{
					std::printf("FAIL input=%08x signed=%d actual=%08x expected=%08x baseline=%08x\n",
						input[j], is_signed, actual[j], expected, baseline[j]);
					return 1;
				}
			}
		}
	}
	std::printf("PASS %llu input patterns for both conversions (%.2fs)\n",
		static_cast<unsigned long long>(limit), std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
	std::fflush(stdout);
	for (bool is_signed : {true, false})
	{
		const conversion old = is_signed ? signed_baseline : unsigned_baseline;
		const conversion candidate = is_signed ? signed_candidate : unsigned_candidate;
		std::vector<double> before, after;
		for (int round = 0; round < 12; round++)
		{
			double x, y;
			if (round % 2)
			{
				y = measure(candidate);
				x = measure(old);
			}
			else
			{
				x = measure(old);
				y = measure(candidate);
			}
			if (round >= 2)
			{
				before.push_back(x);
				after.push_back(y);
			}
		}
		std::sort(before.begin(), before.end());
		std::sort(after.begin(), after.end());
		const double x = (before[4] + before[5]) / 2, y = (after[4] + after[5]) / 2;
		std::printf("%s old=%.3f new=%.3f ns/vector reduction=%.2f%%\n", is_signed ? "signed" : "unsigned", x, y, 100 * (x - y) / x);
		std::fflush(stdout);
	}
}
