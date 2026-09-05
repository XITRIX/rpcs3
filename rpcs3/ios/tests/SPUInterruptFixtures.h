#pragma once

#include <cstddef>
#include <cstdint>

struct interrupt_state
{
	std::uint64_t events;
	std::uint8_t enabled;
	std::uint32_t srr0;
	std::uint32_t branch;
	unsigned calls;
	bool unsupported;
};

constexpr std::uint32_t interrupt_busy_mask = 0x400 | 0x20 | 0x200 | 0x100;
