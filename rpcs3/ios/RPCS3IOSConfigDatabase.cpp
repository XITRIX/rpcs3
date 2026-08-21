#include "RPCS3IOSConfigDatabase.h"

#include "Emu/system_config.h"
#include "Utilities/File.h"

#include "yaml-cpp/yaml.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <mutex>
#include <utility>

namespace rpcs3::ios
{
namespace
{
constexpr std::size_t maximum_config_count = 50'000;
constexpr std::size_t maximum_config_size = 256u * 1024u;

config_database_result failure(config_database_error error, std::string detail)
{
	return {error, 0, 0, std::move(detail)};
}

std::string normalized_title_id(std::string_view title_id)
{
	if (title_id.size() < 9 || title_id.size() > 16)
	{
		return {};
	}

	std::string normalized;
	normalized.reserve(title_id.size());
	for (const unsigned char value : title_id)
	{
		if (!std::isalnum(value) || value >= 0x80)
		{
			return {};
		}
		normalized.push_back(static_cast<char>(std::toupper(value)));
	}
	return normalized;
}
}

config_database_result config_database::load_cache()
{
	const std::string path = cache_path();
	if (!fs::is_file(path))
	{
		return failure(config_database_error::cache_missing,
			"No cached RPCS3 title configuration database is available");
	}

	fs::file file{path, fs::read};
	if (!file)
	{
		return failure(config_database_error::storage_failed,
			"RPCS3 could not open the cached title configuration database");
	}

	const usz size = file.size();
	if (size > config_database_maximum_size)
	{
		return failure(config_database_error::response_too_large,
			"The cached RPCS3 title configuration database exceeds its safety limit");
	}

	const std::string content = file.to_string();
	if (content.size() != size)
	{
		return failure(config_database_error::storage_failed,
			"RPCS3 could not read the complete cached title configuration database");
	}

	config_map configs;
	config_database_result result = parse(content, configs);
	if (result.error == config_database_error::none)
	{
		publish(std::move(configs));
	}
	return result;
}

config_database_result config_database::update(std::string_view content)
{
	config_map configs;
	config_database_result result = parse(content, configs);
	if (result.error != config_database_error::none)
	{
		return result;
	}
	if (result.skipped_configs != 0)
	{
		return failure(config_database_error::invalid_response,
			"RPCS3 refused to replace the cached title configuration database because " +
			std::to_string(result.skipped_configs) + " entries were invalid");
	}

	const std::string path = cache_path();
	if (!fs::create_path(fs::get_config_dir(true)))
	{
		return failure(config_database_error::storage_failed,
			"RPCS3 could not create its configuration database directory");
	}

	fs::pending_file pending{path};
	if (!pending.file ||
		pending.file.write(content.data(), content.size()) != content.size() ||
		!pending.commit())
	{
		return failure(config_database_error::storage_failed,
			"RPCS3 could not atomically save the title configuration database");
	}

	publish(std::move(configs));
	return result;
}

std::string config_database::config_for(std::string_view title_id) const
{
	const std::string normalized = normalized_title_id(title_id);
	if (normalized.empty())
	{
		return {};
	}

	std::shared_lock lock(m_mutex);
	if (const auto found = m_configs.find(normalized); found != m_configs.end())
	{
		return found->second;
	}
	return {};
}

config_database_result config_database::parse(std::string_view content, config_map& configs)
{
	if (content.empty())
	{
		return failure(config_database_error::invalid_response,
			"The RPCS3 title configuration database response is empty");
	}
	if (content.size() > config_database_maximum_size)
	{
		return failure(config_database_error::response_too_large,
			"The RPCS3 title configuration database exceeds its safety limit");
	}
	if (content.find('\0') != std::string_view::npos)
	{
		return failure(config_database_error::invalid_response,
			"The RPCS3 title configuration database contains an embedded null byte");
	}

	try
	{
		const YAML::Node root = YAML::Load(std::string{content});
		if (!root.IsMap())
		{
			return failure(config_database_error::invalid_response,
				"The RPCS3 title configuration database root is not an object");
		}

		const YAML::Node return_code = root["return_code"];
		if (!return_code.IsScalar() || return_code.as<int>() < 0)
		{
			return failure(config_database_error::invalid_response,
				"The RPCS3 title configuration server returned an error");
		}

		const YAML::Node games = root["games"];
		if (!games.IsMap() || games.size() == 0 || games.size() > maximum_config_count)
		{
			return failure(config_database_error::invalid_response,
				"The RPCS3 title configuration database has an invalid game collection");
		}

		std::size_t skipped = 0;
		configs.reserve(games.size());
		for (const auto& entry : games)
		{
			try
			{
				if (!entry.first.IsScalar() || !entry.second.IsMap())
				{
					skipped++;
					continue;
				}

				const std::string title_id = normalized_title_id(entry.first.Scalar());
				const YAML::Node config = entry.second["config"];
				if (title_id.empty() || !config.IsScalar())
				{
					skipped++;
					continue;
				}

				const std::string value = config.Scalar();
				cfg_root verifier;
				if (value.empty() || value.size() > maximum_config_size || !verifier.validate(value))
				{
					skipped++;
					continue;
				}

				if (!configs.emplace(title_id, value).second)
				{
					skipped++;
				}
			}
			catch (const YAML::Exception&)
			{
				skipped++;
			}
		}

		if (configs.empty())
		{
			return failure(config_database_error::invalid_response,
				"The RPCS3 title configuration database contains no usable configurations");
		}

		config_database_result result;
		result.accepted_configs = configs.size();
		result.skipped_configs = skipped;
		result.detail = "Validated " + std::to_string(result.accepted_configs) +
			" RPCS3 title configurations";
		if (skipped != 0)
		{
			result.detail += " and skipped " + std::to_string(skipped) + " invalid entries";
		}
		return result;
	}
	catch (const YAML::Exception& error)
	{
		return failure(config_database_error::invalid_response,
			"RPCS3 could not parse the title configuration database: " + std::string{error.what()});
	}
	catch (const std::exception& error)
	{
		return failure(config_database_error::invalid_response,
			"RPCS3 could not validate the title configuration database: " + std::string{error.what()});
	}
}

std::string config_database::cache_path()
{
	return fs::get_config_dir(true) + "config_database.dat";
}

void config_database::publish(config_map configs)
{
	std::unique_lock lock(m_mutex);
	m_configs = std::move(configs);
}

config_database& shared_config_database()
{
	static config_database database;
	return database;
}
}
