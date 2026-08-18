#include "GameUpdateManifest.h"

#include "Utilities/File.h"

#include <curl/curl.h>

#include <array>
#include <cctype>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace rpcs3::ios
{
namespace
{
std::once_flag g_curl_initialization_once;
CURLcode g_curl_initialization_result = CURLE_FAILED_INIT;

struct response_context
{
	std::string content;
	std::size_t maximum_size = 0;
	bool exceeded_limit = false;
};

struct package_download_context
{
	fs::pending_file destination;
	game_update_download_progress progress;
	std::uint64_t expected_size = 0;
	std::uint64_t downloaded_size = 0;
	bool exceeded_limit = false;
	bool write_failed = false;
};

bool valid_title_id(std::string_view title_id)
{
	if (title_id.size() < 9 || title_id.size() > 16)
	{
		return false;
	}

	for (const unsigned char value : title_id)
	{
		const bool ascii_alphanumeric =
			(value >= 'A' && value <= 'Z') ||
			(value >= 'a' && value <= 'z') ||
			(value >= '0' && value <= '9');
		if (!ascii_alphanumeric)
		{
			return false;
		}
	}
	return true;
}

std::size_t append_response(void* data, std::size_t size, std::size_t count, void* user_context)
{
	auto& context = *static_cast<response_context*>(user_context);
	if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size)
	{
		context.exceeded_limit = true;
		return 0;
	}

	const std::size_t byte_count = size * count;
	if (context.content.size() > context.maximum_size ||
		byte_count > context.maximum_size - context.content.size())
	{
		context.exceeded_limit = true;
		return 0;
	}

	context.content.append(static_cast<const char*>(data), byte_count);
	return byte_count;
}

std::size_t append_package(void* data, std::size_t size, std::size_t count, void* user_context) noexcept
{
	auto& context = *static_cast<package_download_context*>(user_context);
	if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size)
	{
		context.exceeded_limit = true;
		return 0;
	}

	const std::size_t byte_count = size * count;
	if (context.downloaded_size > context.expected_size ||
		byte_count > context.expected_size - context.downloaded_size)
	{
		context.exceeded_limit = true;
		return 0;
	}

	try
	{
		if (context.destination.file.write(data, byte_count) != byte_count)
		{
			context.write_failed = true;
			return 0;
		}
	}
	catch (...)
	{
		context.write_failed = true;
		return 0;
	}

	context.downloaded_size += byte_count;
	return byte_count;
}

int report_package_progress(
	void* user_context,
	curl_off_t,
	curl_off_t downloaded,
	curl_off_t,
	curl_off_t) noexcept
{
	auto& context = *static_cast<package_download_context*>(user_context);
	if (!context.progress)
	{
		return 0;
	}

	const std::uint64_t completed = downloaded <= 0
		? 0
		: std::min<std::uint64_t>(static_cast<std::uint64_t>(downloaded), context.expected_size);
	try
	{
		context.progress(completed, context.expected_size);
	}
	catch (...)
	{
		// A host progress observer must never unwind through libcurl.
	}
	return 0;
}

std::optional<std::string> trusted_package_url(std::string_view raw_url)
{
	if (raw_url.empty())
	{
		return std::nullopt;
	}

	using curl_url_pointer = std::unique_ptr<CURLU, decltype(&curl_url_cleanup)>;
	curl_url_pointer parsed{curl_url(), &curl_url_cleanup};
	if (!parsed)
	{
		return std::nullopt;
	}

	const std::string input{raw_url};
	if (curl_url_set(parsed.get(), CURLUPART_URL, input.c_str(), CURLU_DISALLOW_USER) != CURLUE_OK)
	{
		return std::nullopt;
	}

	char* scheme_value = nullptr;
	char* host_value = nullptr;
	char* port_value = nullptr;
	char* normalized_value = nullptr;
	const CURLUcode scheme_error = curl_url_get(parsed.get(), CURLUPART_SCHEME, &scheme_value, 0);
	const CURLUcode host_error = curl_url_get(parsed.get(), CURLUPART_HOST, &host_value, 0);
	const CURLUcode port_error = curl_url_get(parsed.get(), CURLUPART_PORT, &port_value, CURLU_NO_DEFAULT_PORT);
	const CURLUcode normalized_error = curl_url_get(parsed.get(), CURLUPART_URL, &normalized_value, CURLU_NO_DEFAULT_PORT);

	auto release_parts = [&]
	{
		curl_free(scheme_value);
		curl_free(host_value);
		curl_free(port_value);
		curl_free(normalized_value);
	};

	if (scheme_error != CURLUE_OK || host_error != CURLUE_OK ||
		(port_error != CURLUE_OK && port_error != CURLUE_NO_PORT) ||
		normalized_error != CURLUE_OK)
	{
		release_parts();
		return std::nullopt;
	}

	std::string scheme{scheme_value};
	std::string host{host_value};
	for (char& value : scheme)
	{
		value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
	}
	for (char& value : host)
	{
		value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
	}

	constexpr std::string_view sony_suffix = ".np.dl.playstation.net";
	const bool accepted = scheme == "https" &&
		host.size() > sony_suffix.size() && host.ends_with(sony_suffix) &&
		(!port_value || std::string_view{port_value} == "443");
	std::optional<std::string> result;
	if (accepted)
	{
		result = std::string{normalized_value};
	}
	release_parts();
	return result;
}

game_update_manifest_result failure(game_update_manifest_error error, std::string detail)
{
	return {error, {}, std::move(detail)};
}
}

game_update_manifest_result fetch_game_update_manifest(
	std::string_view title_id,
	std::size_t maximum_size)
{
	if (!valid_title_id(title_id))
	{
		return failure(game_update_manifest_error::invalid_title_id,
			"The game has an invalid PlayStation title ID");
	}
	if (maximum_size == 0)
	{
		return failure(game_update_manifest_error::response_too_large,
			"The update-manifest buffer has no capacity");
	}

	std::call_once(g_curl_initialization_once, []
	{
		g_curl_initialization_result = curl_global_init(CURL_GLOBAL_SSL);
	});
	if (g_curl_initialization_result != CURLE_OK)
	{
		return failure(game_update_manifest_error::initialization_failed,
			std::string{"Unable to initialize the update transport: "} +
			curl_easy_strerror(g_curl_initialization_result));
	}

	using curl_pointer = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
	curl_pointer curl{curl_easy_init(), &curl_easy_cleanup};
	if (!curl)
	{
		return failure(game_update_manifest_error::initialization_failed,
			"Unable to create the update-manifest request");
	}

	std::string normalized_title_id{title_id};
	for (char& value : normalized_title_id)
	{
		if (value >= 'a' && value <= 'z')
		{
			value = static_cast<char>(value - 'a' + 'A');
		}
	}
	const std::string url = "https://a0.ww.np.dl.playstation.net/tpl/np/" +
		normalized_title_id + "/" + normalized_title_id + "-ver.xml";
	response_context response{{}, maximum_size, false};
	std::array<char, CURL_ERROR_SIZE> error_buffer{};

	CURLcode option_error = CURLE_OK;
#define RPCS3_IOS_SET_CURL_OPTION(option, value) \
	do \
	{ \
		if (option_error == CURLE_OK) \
		{ \
			option_error = curl_easy_setopt(curl.get(), option, value); \
		} \
	} while (false)

	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_URL, url.c_str());
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_PROTOCOLS_STR, "https");
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_FOLLOWLOCATION, 0L);
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_SSL_VERIFYPEER, 0L);
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_SSL_VERIFYHOST, 0L);
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_CONNECTTIMEOUT_MS, 15'000L);
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_TIMEOUT_MS, 60'000L);
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_NOSIGNAL, 1L);
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_USERAGENT, "RPCS3-iOS");
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_ACCEPT_ENCODING, "identity");
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_WRITEFUNCTION, &append_response);
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_WRITEDATA, &response);
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_ERRORBUFFER, error_buffer.data());

#undef RPCS3_IOS_SET_CURL_OPTION

	if (option_error != CURLE_OK)
	{
		return failure(game_update_manifest_error::initialization_failed,
			std::string{"Unable to configure the update-manifest request: "} +
			curl_easy_strerror(option_error));
	}

	const CURLcode request_error = curl_easy_perform(curl.get());
	if (response.exceeded_limit)
	{
		return failure(game_update_manifest_error::response_too_large,
			"The PlayStation update manifest exceeded its safety limit");
	}
	if (request_error != CURLE_OK)
	{
		const std::string detail = error_buffer[0]
			? std::string{error_buffer.data()}
			: std::string{curl_easy_strerror(request_error)};
		return failure(game_update_manifest_error::request_failed,
			"Unable to download the PlayStation update manifest: " + detail);
	}

	long status_code = 0;
	if (curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status_code) != CURLE_OK ||
		status_code != 200)
	{
		return failure(game_update_manifest_error::http_error,
			"The PlayStation update server returned HTTP " + std::to_string(status_code));
	}

	return {game_update_manifest_error::none, std::move(response.content), {}};
}

game_update_package_download_result download_game_update_package(
	std::string_view url,
	std::string_view destination_path,
	std::uint64_t expected_size,
	game_update_download_progress progress)
{
	if (destination_path.empty() || expected_size == 0 ||
		expected_size > static_cast<std::uint64_t>(std::numeric_limits<curl_off_t>::max()))
	{
		return {game_update_package_download_error::invalid_destination, 0,
			"The update package destination or expected size is invalid"};
	}

	std::call_once(g_curl_initialization_once, []
	{
		g_curl_initialization_result = curl_global_init(CURL_GLOBAL_SSL);
	});
	if (g_curl_initialization_result != CURLE_OK)
	{
		return {game_update_package_download_error::initialization_failed, 0,
			std::string{"Unable to initialize the update transport: "} +
			curl_easy_strerror(g_curl_initialization_result)};
	}

	const auto normalized_url = trusted_package_url(url);
	if (!normalized_url)
	{
		return {game_update_package_download_error::invalid_url, 0,
			"The update package URL is outside Sony's HTTPS download service"};
	}

	using curl_pointer = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
	curl_pointer curl{curl_easy_init(), &curl_easy_cleanup};
	if (!curl)
	{
		return {game_update_package_download_error::initialization_failed, 0,
			"Unable to create the update-package request"};
	}

	package_download_context response{{destination_path}, std::move(progress), expected_size};
	if (!response.destination.file)
	{
		return {game_update_package_download_error::invalid_destination, 0,
			"Unable to create the temporary update-package file"};
	}
	std::array<char, CURL_ERROR_SIZE> error_buffer{};

	CURLcode option_error = CURLE_OK;
#define RPCS3_IOS_SET_CURL_OPTION(option, value) \
	do \
	{ \
		if (option_error == CURLE_OK) \
		{ \
			option_error = curl_easy_setopt(curl.get(), option, value); \
		} \
	} while (false)

	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_URL, normalized_url->c_str());
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_PROTOCOLS_STR, "https");
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_FOLLOWLOCATION, 0L);
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_SSL_VERIFYPEER, 0L);
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_SSL_VERIFYHOST, 0L);
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_CONNECTTIMEOUT_MS, 15'000L);
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_TIMEOUT_MS, 6L * 60L * 60L * 1'000L);
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_NOSIGNAL, 1L);
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_USERAGENT, "RPCS3-iOS");
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_ACCEPT_ENCODING, "identity");
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(expected_size));
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_WRITEFUNCTION, &append_package);
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_WRITEDATA, &response);
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_NOPROGRESS, 0L);
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_XFERINFOFUNCTION, &report_package_progress);
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_XFERINFODATA, &response);
	RPCS3_IOS_SET_CURL_OPTION(CURLOPT_ERRORBUFFER, error_buffer.data());

#undef RPCS3_IOS_SET_CURL_OPTION

	if (option_error != CURLE_OK)
	{
		return {game_update_package_download_error::initialization_failed, 0,
			std::string{"Unable to configure the update-package request: "} +
			curl_easy_strerror(option_error)};
	}

	const CURLcode request_error = curl_easy_perform(curl.get());
	if (response.exceeded_limit || request_error == CURLE_FILESIZE_EXCEEDED)
	{
		return {game_update_package_download_error::response_too_large,
			response.downloaded_size, "The update package exceeded its manifest-declared size"};
	}
	if (response.write_failed)
	{
		return {game_update_package_download_error::write_failed,
			response.downloaded_size, "Unable to write the update package to temporary storage"};
	}
	if (request_error != CURLE_OK)
	{
		const std::string detail = error_buffer[0]
			? std::string{error_buffer.data()}
			: std::string{curl_easy_strerror(request_error)};
		return {game_update_package_download_error::request_failed,
			response.downloaded_size, "Unable to download the PlayStation update package: " + detail};
	}

	long status_code = 0;
	if (curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status_code) != CURLE_OK ||
		status_code != 200)
	{
		return {game_update_package_download_error::http_error, response.downloaded_size,
			"The PlayStation update server returned HTTP " + std::to_string(status_code)};
	}
	if (response.downloaded_size != expected_size)
	{
		return {game_update_package_download_error::size_mismatch, response.downloaded_size,
			"The update package size does not match Sony's manifest"};
	}
	if (!response.destination.commit(false))
	{
		return {game_update_package_download_error::write_failed, response.downloaded_size,
			"Unable to commit the downloaded update package"};
	}
	if (response.progress)
	{
		response.progress(expected_size, expected_size);
	}
	return {game_update_package_download_error::none, response.downloaded_size, {}};
}
}
