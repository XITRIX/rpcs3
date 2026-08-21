#pragma once

#include <cstddef>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace rpcs3::ios
{
inline constexpr std::size_t config_database_maximum_size = 16u * 1024u * 1024u;

enum class config_database_error
{
	none,
	cache_missing,
	response_too_large,
	invalid_response,
	storage_failed,
};

struct config_database_result
{
	config_database_error error = config_database_error::none;
	std::size_t accepted_configs = 0;
	std::size_t skipped_configs = 0;
	std::string detail;
};

class config_database final
{
public:
	config_database_result load_cache();
	config_database_result update(std::string_view content);
	std::string config_for(std::string_view title_id) const;

private:
	using config_map = std::unordered_map<std::string, std::string>;

	static config_database_result parse(std::string_view content, config_map& configs);
	static std::string cache_path();
	void publish(config_map configs);

	mutable std::shared_mutex m_mutex;
	config_map m_configs;
};

config_database& shared_config_database();
}
