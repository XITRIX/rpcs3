#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace rpcs3::ios
{
enum class game_update_manifest_error
{
	none,
	invalid_title_id,
	initialization_failed,
	request_failed,
	http_error,
	response_too_large,
};

struct game_update_manifest_result
{
	game_update_manifest_error error = game_update_manifest_error::request_failed;
	std::string content;
	std::string detail;
};

// Sony's legacy manifest endpoint uses a certificate that current Apple TLS
// rejects before URLSession can apply an authentication-challenge override.
// This focused transport uses RPCS3's bundled curl/wolfSSL stack and disables
// certificate verification only for the fixed URL constructed in this helper.
game_update_manifest_result fetch_game_update_manifest(
	std::string_view title_id,
	std::size_t maximum_size);

enum class game_update_package_download_error
{
	none,
	invalid_url,
	invalid_destination,
	initialization_failed,
	request_failed,
	http_error,
	response_too_large,
	size_mismatch,
	write_failed,
};

struct game_update_package_download_result
{
	game_update_package_download_error error = game_update_package_download_error::request_failed;
	std::uint64_t downloaded_size = 0;
	std::string detail;
};

using game_update_download_progress = std::function<void(std::uint64_t, std::uint64_t)>;

// Sony package URLs use the same legacy delivery service as the manifest.
// Accept only HTTPS URLs on a Sony download subdomain, never redirect, write
// atomically, and refuse to consume more than the manifest-declared size.
game_update_package_download_result download_game_update_package(
	std::string_view url,
	std::string_view destination_path,
	std::uint64_t expected_size,
	game_update_download_progress progress);
}
