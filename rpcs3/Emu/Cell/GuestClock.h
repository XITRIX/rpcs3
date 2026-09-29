#pragma once

#include "util/atomic.hpp"
#include <algorithm>
#include <mutex>
#include <thread>

// Map the host monotonic clock to guest time, excluding emulator pauses and
// explicitly bracketed host work that blocks an emulated producer.
// Host and guest anchors are published together: readers must never combine a
// new resume anchor with the previous accumulated pause duration.
class guest_clock
{
	static constexpr u64 paused_bit = u64{1} << 63;
	static constexpr u64 updating_bit = u64{1} << 62;

	struct clock_state
	{
		u64 host = 0;
		u64 guest = 0;
	};

	atomic_t<clock_state> m_state{};
	// Writers are infrequent lifecycle/compilation boundaries. Readers keep
	// the atomic snapshot fast path and wait only for anchor publication.
	std::mutex m_mutex;
	u64 m_generation = 1;
	u64 m_holds = 0;
	bool m_paused = false;

	template <typename Clock>
	void update(Clock now, bool paused, u64 holds)
	{
		auto state = m_state.load();
		const bool held = paused || holds;
		if (held == !!(state.host & paused_bit))
		{
			return;
		}

		// Block new snapshots BEFORE sampling the transition time. Sampling
		// before the writer lock can rewind the clock if that writer is delayed
		// behind a newer resume (and underflow an unsigned guest deadline).
		m_state.store({state.host | updating_bit, state.guest});
		u64 host_us;
		try
		{
			host_us = std::max(now(), state.host & ~paused_bit);
		}
		catch (...)
		{
			m_state.store(state);
			throw;
		}
		if (held)
		{
			state.guest = read(state, host_us, 1);
		}
		state.host = host_us | (held ? paused_bit : 0);
		m_state.store(state);
	}

	static u64 read(clock_state state, u64 host_ticks, u64 ticks_per_us)
	{
		const u64 origin = (state.host & ~paused_bit) * ticks_per_us;
		const u64 elapsed = !(state.host & paused_bit) && host_ticks > origin ? host_ticks - origin : 0;
		return state.guest * ticks_per_us + elapsed;
	}

public:
	u64 get(u64 host_ticks, u64 ticks_per_us = 1) const
	{
		for (;;)
		{
			const auto state = m_state.load();
			if (!(state.host & updating_bit))
			{
				return read(state, host_ticks, ticks_per_us);
			}
			std::this_thread::yield();
		}
	}

	template <typename Clock>
	void pause(Clock now)
	{
		std::lock_guard lock(m_mutex);
		update(now, true, m_holds);
		m_paused = true;
	}

	template <typename Clock>
	void resume(Clock now)
	{
		std::lock_guard lock(m_mutex);
		update(now, false, m_holds);
		m_paused = false;
	}

	// Independent, nestable host-work holds must not release a manual pause
	// or one another. Tokens from before a boot/save-state reset are inert.
	template <typename Clock>
	u64 hold(Clock now)
	{
		std::lock_guard lock(m_mutex);
		update(now, m_paused, m_holds + 1);
		++m_holds;
		return m_generation;
	}

	template <typename Clock>
	void release(u64 generation, Clock now)
	{
		std::lock_guard lock(m_mutex);
		if (generation == m_generation && m_holds)
		{
			update(now, m_paused, m_holds - 1);
			--m_holds;
		}
	}

	// Called with the other timebase initialization, before guest threads run.
	void reset()
	{
		std::lock_guard lock(m_mutex);
		if (!++m_generation) ++m_generation;
		m_holds = 0;
		m_paused = false;
		m_state.store({});
	}
};
