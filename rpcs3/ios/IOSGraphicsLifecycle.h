#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

namespace rpcs3::ios
{
// UIKit serializes set_active on its main thread. Compilation remains independent
// of this gate: only optional drawing and actual GPU submissions participate.
class graphics_lifecycle
{
	std::mutex m_mutex;
	std::condition_variable m_changed;
	std::atomic<bool> m_active{true};
	std::atomic<std::uint64_t> m_generation{1};
	bool m_suspended = false;
	unsigned m_frames = 0;
	unsigned m_submissions = 0;
	using drain_callback = void (*)(void*);
	std::vector<std::pair<void*, drain_callback>> m_devices;

	void release(bool frame)
	{
		std::lock_guard lock(m_mutex);
		--(frame ? m_frames : m_submissions);
		m_changed.notify_all();
	}

public:
	class scope
	{
		graphics_lifecycle* m_owner;
		bool m_frame;
	public:
		scope(graphics_lifecycle* owner, bool frame) : m_owner(owner), m_frame(frame) {}
		scope(const scope&) = delete;
		scope& operator=(const scope&) = delete;
		scope(scope&& other) noexcept : m_owner(std::exchange(other.m_owner, nullptr)), m_frame(other.m_frame) {}
		~scope() { if (m_owner) m_owner->release(m_frame); }
		explicit operator bool() const { return m_owner != nullptr; }
	};

	bool active() const { return m_active.load(std::memory_order_acquire); }
	std::uint64_t generation() const { return m_generation.load(std::memory_order_acquire); }

	scope try_begin_frame()
	{
		std::lock_guard lock(m_mutex);
		if (!m_active.load(std::memory_order_relaxed)) return {nullptr, true};
		++m_frames;
		return {this, true};
	}

	scope begin_submission()
	{
		std::unique_lock lock(m_mutex);
		m_changed.wait(lock, [this] { return !m_suspended; });
		++m_submissions;
		return {this, false};
	}

	void register_device(void* device, drain_callback drain)
	{
		std::lock_guard lock(m_mutex);
		m_devices.emplace_back(device, drain);
	}

	void unregister_device(void* device)
	{
		std::lock_guard lock(m_mutex);
		std::erase_if(m_devices, [device](const auto& entry) { return entry.first == device; });
	}

	void set_active(bool active)
	{
		std::unique_lock lock(m_mutex);
		if (m_active.load(std::memory_order_relaxed) == active) return;
		if (active)
		{
			m_suspended = false;
			m_generation.fetch_add(1, std::memory_order_release);
			m_active.store(true, std::memory_order_release);
			m_changed.notify_all();
			return;
		}

		// Reject optional frames, but allow their already-admitted work,
		// including MTRSX offloader submissions, to finish while foreground.
		m_active.store(false, std::memory_order_release);
		m_changed.wait(lock, [this] { return m_frames == 0; });
		m_suspended = true;
		m_changed.wait(lock, [this] { return m_submissions == 0; });
		// Device registration/destruction cannot race this final GPU drain.
		for (const auto& [device, drain] : m_devices) drain(device);
	}
};

graphics_lifecycle& graphics_lifecycle_state();
void initialize_graphics_lifecycle();
}
