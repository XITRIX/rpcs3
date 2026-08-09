#pragma once

#include <algorithm>
#include <cstdint>

namespace rpcs3::ios::audio
{
constexpr std::uint32_t callback_byte_count(
	std::uint32_t frame_count,
	std::uint32_t bytes_per_frame,
	std::uint32_t buffer_capacity) noexcept
{
	const std::uint64_t requested =
		static_cast<std::uint64_t>(frame_count) * bytes_per_frame;
	return static_cast<std::uint32_t>(std::min<std::uint64_t>(requested, buffer_capacity));
}

constexpr std::uint32_t aligned_written_byte_count(
	std::uint32_t written,
	std::uint32_t requested,
	std::uint32_t bytes_per_frame) noexcept
{
	if (bytes_per_frame == 0)
	{
		return 0;
	}

	const std::uint32_t clamped = std::min(written, requested);
	return clamped - clamped % bytes_per_frame;
}
}
