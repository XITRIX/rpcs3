#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace rpcs3::ios
{
inline std::string analysis_timing_text(uint64_t elapsed_seconds, uint64_t idle_seconds)
{
	const auto seconds = elapsed_seconds % 60;
	return "Elapsed " + std::to_string(elapsed_seconds / 60) + ":" + (seconds < 10 ? "0" : "") +
		std::to_string(seconds) + " | Last progress " + std::to_string(idle_seconds) + "s ago | No time estimate";
}

// Analysis discovers more work as it walks code. Report visits and the current
// queue size, never treat that changing queue as a fixed percentage or ETA.
class ppu_analysis_progress final
{
public:
	using clock = std::chrono::steady_clock;
	using publisher = std::function<void(std::string)>;

	ppu_analysis_progress(std::string module, publisher publish)
		: m_module(std::move(module)), m_publish(std::move(publish))
	{
	}

	void update(std::string_view phase, uint64_t visited = 0, uint64_t discovered = 0,
		clock::time_point now = clock::now())
	{
		if (!m_phase.empty() && now - m_last_update < std::chrono::milliseconds(100))
		{
			return;
		}
		m_phase = phase;
		m_last_update = now;
		std::string text = "Analyzing PPU Executable...";
		if (!m_module.empty()) text += "\n" + m_module;
		text += "\n";
		text += phase;
		if (discovered)
		{
			text += ": " + std::to_string(visited) + " / " + std::to_string(discovered);
		}
		m_publish(std::move(text));
	}

private:
	std::string m_module;
	publisher m_publish;
	std::string m_phase;
	clock::time_point m_last_update{};
};
}
