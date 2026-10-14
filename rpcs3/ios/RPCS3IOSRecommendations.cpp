#include "RPCS3IOSRecommendations.h"
#include "RPCS3IOSConfigDatabase.h"
#include "RPCS3IOSRuntimePatches.h"

#include "yaml-cpp/yaml.h"

#include <algorithm>
#include <array>
#include <utility>

namespace rpcs3::ios
{
namespace
{
struct port_recommendation_rule
{
	std::string_view title_id;
	std::string_view version;
	std::string_view setting_key;
	std::string_view section;
	std::string_view name;
	// Empty for recommendations that do not require a runtime patch.
	std::string_view patch_hash;
	std::string_view patch_description;
	std::string_view enabled_value;
	std::string_view disabled_value;
};

constexpr auto collision_rule(std::string_view title_id, std::string_view version)
{
	return port_recommendation_rule{title_id, version,
		"cpu.spu_xfloat_accuracy", "Core", "SPU XFloat Accuracy",
		"SPU-d0a1508208613e2184fbd64906d34d5f07890033",
		"ARMSX3 Approximate collision fix", "Approximate", "Accurate"};
}

// Add future port rules here with exact version coverage and prerequisites.
constexpr std::array rules{
	collision_rule("BLUS30682", "01.02"),
	collision_rule("BLES01128", "01.02"),
	collision_rule("BLUS30127", "01.08"),
	collision_rule("NPUB30702", "01.08"),
	// User-observed BLES01807 01.27 performance improvement with Relaxed.
	port_recommendation_rule{"BLES01807", "01.27",
		"cpu.spu_xfloat_accuracy", "Core", "SPU XFloat Accuracy",
		{}, {}, "Relaxed", {}},
};
}

title_recommendations recommendations_for_title(
	std::string_view title_id, std::string_view installed_version)
{
	title_recommendations result{shared_config_database().config_for(title_id), {}};
	const bool has_rule = std::ranges::any_of(rules, [&](const auto& rule)
	{
		return rule.title_id == title_id && rule.version == installed_version;
	});
	if (!has_rule)
	{
		return result;
	}

	// Only patch-dependent rules consult the repository. A missing, malformed
	// or disabled collision patch must retain GTA IV's Accurate XFloat.
	const bool needs_patches = std::ranges::any_of(rules, [&](const auto& rule)
	{
		return rule.title_id == title_id && rule.version == installed_version &&
			!rule.patch_hash.empty();
	});
	const auto patches = needs_patches
		? runtime_patches_for_title(title_id, installed_version) : runtime_patch_list_result{};
	YAML::Node merged = result.config.empty()
		? YAML::Node{YAML::NodeType::Map} : YAML::Load(result.config);
	for (const auto& rule : rules)
	{
		if (rule.title_id != title_id || rule.version != installed_version)
		{
			continue;
		}
		const bool enabled = rule.patch_hash.empty() ||
			(patches.error == runtime_patch_update_error::none &&
				std::ranges::any_of(patches.patches, [&](const auto& patch)
				{
					return patch.enabled && patch.hash == rule.patch_hash &&
						patch.description == rule.patch_description &&
						patch.app_version == rule.version;
				}));
		merged[std::string{rule.section}][std::string{rule.name}] =
			std::string{enabled ? rule.enabled_value : rule.disabled_value};
		result.port_keys.push_back(rule.setting_key);
	}
	result.config = YAML::Dump(merged);
	return result;
}
}
