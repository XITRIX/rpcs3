#include "SPUInterruptFixtures.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>

extern "C" std::uint32_t spu_interrupt_fast(interrupt_state*, std::uint32_t);
extern "C" std::uint32_t spu_interrupt_native(interrupt_state*, std::uint32_t);
extern "C" std::uint32_t spu_bench_fast(interrupt_state*, std::uint32_t);
extern "C" std::uint32_t spu_bench_native(interrupt_state*, std::uint32_t);

// Models the unmodified native helper's observable state transitions. The
// unsupported-mask sentinel stands for the original throwing path, which the
// production fast path must continue to call before modifying enabled.
static bool __attribute__((noinline)) set_interrupt_status(interrupt_state* state)
{
	if ((__atomic_load_n(&state->events, __ATOMIC_SEQ_CST) >> 32) & interrupt_busy_mask)
	{
		state->unsupported = true;
		return false;
	}
	state->enabled = true;
	return true;
}

template <bool CountCalls>
static std::uint32_t check_interrupts(interrupt_state* state, std::uint32_t address)
{
	if constexpr (CountCalls) state->calls++;
	if (!set_interrupt_status(state)) return 0xffffffff;
	if (__atomic_load_n(&state->events, __ATOMIC_SEQ_CST) & (std::uint64_t{1} << 31))
	{
		state->enabled = false;
		state->srr0 = address;
		if ((state->branch & 0xfd80007f) == 0x30000000) return (state->branch >> 5) & 0x3fffc;
		return 0;
	}
	return address;
}

extern "C" std::uint32_t __attribute__((noinline)) spu_test_check_interrupts(interrupt_state* state, std::uint32_t address)
{
	return check_interrupts<true>(state, address);
}

extern "C" std::uint32_t __attribute__((noinline)) spu_bench_check_interrupts(interrupt_state* state, std::uint32_t address)
{
	// Timing excludes the correctness probe's call counter.
	return check_interrupts<false>(state, address);
}

static void check(const interrupt_state& initial, std::uint32_t address)
{
	auto fast = initial;
	auto native = initial;
	const auto lhs = spu_interrupt_fast(&fast, address);
	const auto rhs = spu_interrupt_native(&native, address);
	const bool slow = ((initial.events >> 32) & interrupt_busy_mask) || (initial.events & (std::uint64_t{1} << 31));
	if (lhs != rhs || fast.enabled != native.enabled || fast.srr0 != native.srr0 ||
		fast.unsupported != native.unsupported || fast.events != initial.events ||
		fast.calls != initial.calls + slow || native.calls != initial.calls + 1)
	{
		std::fputs("SPU interrupt fast path changed the native transition\n", stderr);
		std::abort();
	}
}

static double measure(decltype(&spu_interrupt_fast) function)
{
	interrupt_state state{};
	constexpr unsigned iterations = 5'000'000;
	unsigned sum = 0;
	const auto start = std::chrono::steady_clock::now();
	for (unsigned i = 0; i < iterations; i++) sum += function(&state, 0x1234);
	const auto end = std::chrono::steady_clock::now();
	if (sum != iterations * 0x1234u || !state.enabled) std::abort();
	return std::chrono::duration<double, std::nano>(end - start).count() / iterations;
}

int main(int argc, char**)
{
	// Every mask, both count values and previous enable states; include the
	// supported SN event, ignored event/lock/wait bits, and BR/BRA/non-branches.
	for (std::uint64_t mask = 0; mask < 65536; mask++)
	{
		for (unsigned flags = 0; flags < 4; flags++)
		{
			interrupt_state state{(mask << 32) | 0x40ffffff | (std::uint64_t{flags & 1} << 31),
				static_cast<std::uint8_t>(flags >> 1), 0x9988, 0x30000000u | (0x1234u << 5), 0, false};
			check(state, 0x3210);
			state.branch = 0xffffffff;
			check(state, 0x3fffc);
		}
	}
	std::mt19937_64 random(0x535055);
	for (unsigned trial = 0; trial < 100000; trial++)
	{
		interrupt_state state{random(), static_cast<std::uint8_t>(random() & 1),
			static_cast<std::uint32_t>(random()), static_cast<std::uint32_t>(random()), 0, false};
		check(state, random() & 0x3fffc);
	}
	std::puts("SPU interrupts: 524288 mask/count/enable/branch cases and 100000 random cases passed; two seq_cst loads retained");
	if (argc > 1)
	{
		std::array<double, 9> native{}, fast{};
		for (unsigned round = 0; round < native.size(); round++)
		{
			if (round % 2)
			{
				fast[round] = measure(spu_bench_fast);
				native[round] = measure(spu_bench_native);
			}
			else
			{
				native[round] = measure(spu_bench_native);
				fast[round] = measure(spu_bench_fast);
			}
		}
		std::sort(native.begin(), native.end());
		std::sort(fast.begin(), fast.end());
		std::printf("No-event interrupt check: native %.3f ns, generated fast path %.3f ns (%.2fx)\n",
			native[4], fast[4], native[4] / fast[4]);
	}
}
