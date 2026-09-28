#pragma once

#include "util/atomic.hpp"

// Map the host monotonic clock to time which advances only during emulation.
// Host and guest anchors are published together: readers must never combine a
// new resume anchor with the previous accumulated pause duration.
class guest_clock
{
	static constexpr u64 paused_bit = u64{1} << 63;

	struct clock_state
	{
		u64 host = 0;
		u64 guest = 0;
	};

	atomic_t<clock_state> m_state{};

	static u64 read(clock_state state, u64 host_ticks, u64 ticks_per_us)
	{
		const u64 origin = (state.host & ~paused_bit) * ticks_per_us;
		const u64 elapsed = !(state.host & paused_bit) && host_ticks > origin ? host_ticks - origin : 0;
		return state.guest * ticks_per_us + elapsed;
	}

public:
	u64 get(u64 host_ticks, u64 ticks_per_us = 1) const
	{
		return read(m_state.load(), host_ticks, ticks_per_us);
	}

	void pause(u64 host_us)
	{
		m_state.atomic_op([&](clock_state& state)
		{
			if (!(state.host & paused_bit))
			{
				state.guest = read(state, host_us, 1);
				state.host = host_us | paused_bit;
			}
		});
	}

	void resume(u64 host_us)
	{
		m_state.atomic_op([&](clock_state& state)
		{
			if (state.host & paused_bit)
			{
				state.host = host_us;
			}
		});
	}

	// Called with the other timebase initialization, before guest threads run.
	void reset()
	{
		m_state.store({});
	}
};
