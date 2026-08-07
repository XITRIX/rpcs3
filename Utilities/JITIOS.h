#pragma once

#include "util/types.hpp"

namespace rpcs3::ios::jit
{
inline constexpr u64 protocol_response = 0x52504353334a4954ull;
inline constexpr u16 breakpoint_immediate = 0x5250;
inline constexpr u16 protocol_magic = 0x5253;

bool is_ready() noexcept;
void* reserve(usz size) noexcept;
bool commit(void* executable, usz size) noexcept;
void* writable(const void* executable, usz size = 1) noexcept;
void flush(const void* executable, usz size) noexcept;
void release(void* executable, usz size) noexcept;
const char* last_error() noexcept;
}
