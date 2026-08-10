#pragma once

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <span>
#include <vector>

namespace rpcs3::ios
{
class overlay_pcm_buffer
{
public:
	explicit overlay_pcm_buffer(std::size_t capacity_samples)
		: m_samples(std::max<std::size_t>(capacity_samples, 1))
	{
	}

	std::size_t write(std::span<const float> source)
	{
		std::lock_guard lock{m_mutex};
		const std::size_t count = std::min(source.size(), m_samples.size() - m_size);
		const std::size_t first = std::min(count, m_samples.size() - m_write_position);
		std::copy_n(source.begin(), first, m_samples.begin() + m_write_position);
		std::copy_n(source.begin() + first, count - first, m_samples.begin());
		m_write_position = (m_write_position + count) % m_samples.size();
		m_size += count;
		return count;
	}

	std::size_t read(std::span<float> destination) noexcept
	{
		std::unique_lock lock{m_mutex, std::try_to_lock};
		if (!lock)
		{
			return 0;
		}

		const std::size_t count = std::min(destination.size(), m_size);
		const std::size_t first = std::min(count, m_samples.size() - m_read_position);
		std::copy_n(m_samples.begin() + m_read_position, first, destination.begin());
		std::copy_n(m_samples.begin(), count - first, destination.begin() + first);
		m_read_position = (m_read_position + count) % m_samples.size();
		m_size -= count;
		return count;
	}

	void clear() noexcept
	{
		std::lock_guard lock{m_mutex};
		m_read_position = 0;
		m_write_position = 0;
		m_size = 0;
	}

	std::size_t size() const noexcept
	{
		std::lock_guard lock{m_mutex};
		return m_size;
	}

	std::size_t capacity() const noexcept
	{
		return m_samples.size();
	}

private:
	mutable std::mutex m_mutex;
	std::vector<float> m_samples;
	std::size_t m_read_position = 0;
	std::size_t m_write_position = 0;
	std::size_t m_size = 0;
};
}
