#include "util/logs.hpp"
#include <mutex>
#include <stdexcept>
#include <vector>
#include <cstdio>

namespace logs
{
	// Only the registry and final transport are test doubles. The real channel,
	// message severity/filter/dispatch, and reset/silence code run below.
	struct recording_logger
	{
		std::map<std::string, channel*> channels;
		std::vector<std::pair<level, std::string>> messages;
	};

	recording_logger* get_logger()
	{
		static recording_logger logger;
		return &logger;
	}

	std::mutex g_mutex;
	registerer::registerer(channel& ch)
	{
		get_logger()->channels.emplace(ch.name, &ch);
	}

	void message::broadcast(const char* text, const fmt_type_info*, ...) const
	{
		get_logger()->messages.emplace_back(static_cast<level>(*this), text);
	}

#include "LoggingControlsUnderTest.inc"
}

int main()
{
	static logs::channel renderer{"renderer"};
	static logs::channel cpu{"cpu"};
	logs::registerer register_renderer{renderer}, register_cpu{cpu};
	auto& messages = logs::get_logger()->messages;
	const auto require = [](bool condition)
	{
		if (!condition) throw std::runtime_error("Logging contract failed");
	};

	logs::silence();
	logs::silence(); // Reapplying native-menu settings keeps the fatal gate.
	renderer.fatal("graphics failure");
	cpu.fatal("CPU failure");
#ifdef RPCS3_IOS
	require(messages.size() == 2);
	require(messages[0] == std::pair{logs::level::fatal, std::string{"graphics failure"}});
	require(messages[1] == std::pair{logs::level::fatal, std::string{"CPU failure"}});
#else
	require(messages.empty());
#endif
	messages.clear();
	renderer.error("error");
	renderer.todo("todo");
	renderer.success("success");
	renderer.warning("warning");
	renderer.notice("notice");
	renderer.trace("trace");
	require(messages.empty());
	renderer.always()("always");
	require(messages.size() == 1 && messages[0].first == logs::level::always);

	messages.clear();
	logs::reset();
	renderer.fatal("fatal after reset");
	renderer.error("error after reset");
	renderer.notice("notice after reset");
	renderer.trace("trace after reset");
	require(messages.size() == 3);
	std::puts("PASS: production silence/reset, fatal delivery and routine filtering");
}
