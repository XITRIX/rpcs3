#pragma once

#include <mutex>
#include <unordered_map>

namespace rpcs3::ios
{
// Notifications and backend creation/destruction run on different threads.
// Hold the registry lock through delivery so detach waits for in-flight calls.
// Handlers must not attach/detach or call set_active recursively.
class audio_session_lifecycle
{
public:
	using handler = void (*)(void*, bool);

	void attach(void* context, handler callback)
	{
		std::lock_guard lock{m_mutex};
		m_handlers.emplace(context, callback);
		callback(context, m_active);
	}

	void detach(void* context)
	{
		std::lock_guard lock{m_mutex};
		m_handlers.erase(context);
	}

	void set_active(bool active)
	{
		std::lock_guard lock{m_mutex};
		if (m_active == active) return;
		m_active = active;
		for (const auto& [context, callback] : m_handlers)
		{
			callback(context, active);
		}
	}

private:
	std::mutex m_mutex;
	std::unordered_map<void*, handler> m_handlers;
	bool m_active = false;
};

audio_session_lifecycle& audio_session_state();
}
