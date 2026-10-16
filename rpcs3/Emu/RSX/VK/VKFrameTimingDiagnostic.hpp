#pragma once

#ifdef RPCS3_IOS
#include "Utilities/File.h"
#include <array>
#include <cstdlib>
#include <string>
#include <vector>

// Temporary, opt-in diagnosis. One RSX thread owns this bounded capture;
// export happens only after its measurement window, never per frame.
class vk_frame_timing_diagnostic
{
	struct capture
	{
		bool enabled = [] { const char* value = std::getenv("RPCS3_DIAG_FRAME_PHASES"); const bool enabled = value && std::string_view(value) == "1"; rsx_log.notice("Frame phase diagnostic enabled=%d", enabled); return enabled; }();
		bool closed = false;
		u64 first = 0;
		std::vector<std::array<u64, 6>> rows;
	};
	static capture& data() { static thread_local capture value; return value; }
	std::array<u64, 6> row{};
	bool active = false;
public:
	explicit vk_frame_timing_diagnostic(bool guest_frame)
	{
		auto& c = data();
		active = guest_frame && c.enabled && !c.closed;
		if (!active) return;
		row[0] = get_system_time();
		if (!c.first) { c.first = row[0]; c.rows.reserve(24000); rsx_log.notice("Frame phase diagnostic first guest flip=%llu", c.first); }
	}
	void mark(unsigned phase) { if (active) row[phase] = get_system_time(); }
	~vk_frame_timing_diagnostic()
	{
		if (!active) return;
		auto& c = data();
		row[5] = get_system_time();
		c.rows.push_back(row);
		if (row[5] - c.first < 610'000'000 && c.rows.size() < 24000) return;
		c.closed = true;
		{
			std::string csv = "begin_us,before_acquire_us,after_acquire_us,before_submit_us,after_submit_us,end_us\n";
			csv.reserve(c.rows.size() * 110);
			for (const auto& r : c.rows)
			{
				for (unsigned i = 0; i < r.size(); ++i) { if (i) csv += ','; csv += std::to_string(r[i]); }
				csv += '\n';
			}
			fs::file out{fs::get_cache_dir() + "rsx-frame-phases-" + std::to_string(c.first) + ".csv", fs::rewrite};
			if (out) out.write(csv);
			rsx_log.notice("Frame phase diagnostic export rows=%u success=%d", c.rows.size(), static_cast<bool>(out));
		}
	}
};
#endif
