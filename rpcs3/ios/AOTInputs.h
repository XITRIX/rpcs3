#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <vector>

namespace rpcs3::ios::aot
{
	inline bool executable_input_name(const std::filesystem::path& file)
	{
		auto extension = file.extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c)
			{
				return std::tolower(c);
			});
		return extension == ".bin" || extension == ".self" || extension == ".sprx" || extension == ".prx" || extension == ".elf";
	}

	// Installed disc titles may resolve to an ISO file, whereas extracted titles
	// resolve to directories. Hash the complete image rather than traversing it or
	// trusting sidecar preview metadata; directory titles select executable files.
	inline void collect_inputs(const std::filesystem::path& source, bool all,
		std::vector<std::filesystem::path>& inputs, const std::function<void()>& checkpoint = {})
	{
		namespace fs = std::filesystem;
		if (!fs::exists(source))
			return;
		if (fs::is_symlink(fs::symlink_status(source)))
			throw std::runtime_error("Symlinked AOT source is unsupported");
		const auto add = [&](const fs::path& file)
		{
			if (inputs.size() >= 8192)
				throw std::runtime_error("Too many executable AOT inputs");
			inputs.push_back(file);
		};
		if (fs::is_regular_file(source))
		{
			add(source);
			return;
		}
		if (!fs::is_directory(source))
			throw std::runtime_error("Unsupported AOT source type");
		for (const auto& file : fs::recursive_directory_iterator(source))
		{
			if (checkpoint)
				checkpoint();
			if (file.is_symlink())
				throw std::runtime_error("Symlinked AOT source is unsupported");
			if (!file.is_regular_file())
				continue;
			if (all || executable_input_name(file.path()))
				add(file.path());
		}
	}
} // namespace rpcs3::ios::aot
