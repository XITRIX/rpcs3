#pragma once

#include "util/types.hpp"

u64 convert_to_timebased_time(u64 time);
u64 get_timebased_time();

// Returns some relative time in microseconds, don't change this fact
u64 get_system_time();

// Unscaled emulation time: excludes pauses and blocking host compilation,
// but retains the host clock epoch. Host watchdogs use get_system_time().
u64 get_active_system_time(u64 time = umax);
void pause_guest_time();
void resume_guest_time();

u64 begin_guest_time_stall();
void end_guest_time_stall(u64 generation, u64 started, const char* reason);

// Only bracket host work that blocks an emulated CPU/RSX producer. Ordinary
// guest waits, GPU execution, frame pacing and asynchronous work are not holds.
// This must be destroyed BEFORE any non-C++ JIT escape/longjmp.
class guest_time_stall
{
	const char* m_reason;
	u64 m_started;
	u64 m_generation;

public:
	explicit guest_time_stall(const char* reason, bool enabled = true)
		: m_reason(reason)
		, m_started(enabled ? get_system_time() : 0)
		, m_generation(enabled ? begin_guest_time_stall() : 0)
	{
	}

	~guest_time_stall()
	{
		if (m_generation) end_guest_time_stall(m_generation, m_started, m_reason);
	}

	guest_time_stall(const guest_time_stall&) = delete;
	guest_time_stall& operator=(const guest_time_stall&) = delete;
};

// As get_system_time but obeys Clocks scaling setting. Microseconds.
u64 get_guest_system_time(u64 time = umax);
