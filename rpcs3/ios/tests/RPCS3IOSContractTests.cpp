#include "../RPCS3IOSContract.h"
#include "../RPCS3IOSDisplay.h"

#include <cassert>

int main()
{
	using namespace rpcs3::ios;
	static_assert(RPCS3_IOS_ABI_VERSION == 20);
	static_assert(RPCS3_IOS_FOLDER_INVALID == 22);
	static_assert(RPCS3_IOS_FOLDER_INSTALL_FAILED == 23);
	static_assert(RPCS3_IOS_PATCH_INVALID == 24);
	static_assert(RPCS3_IOS_PATCH_TITLE_MISMATCH == 25);
	static_assert(RPCS3_IOS_PATCH_INSTALL_FAILED == 26);
	static_assert(RPCS3_IOS_PATCH_REPOSITORY_INVALID == 27);
	static_assert(RPCS3_IOS_RUNTIME_PATCH_SAVE_FAILED == 30);
	static_assert(RPCS3_IOS_RAP_INVALID == 35);
	static_assert(RPCS3_IOS_RAP_INSTALL_FAILED == 36);
	static_assert(sizeof(rpcs3_ios_display_surface) == 24);
	static_assert(sizeof(rpcs3_ios_pad_state) == 40);
	static_assert(sizeof(rpcs3_ios_pad_feedback) == 16);
	static_assert(sizeof(rpcs3_ios_game_info) == 96);
	static_assert(sizeof(rpcs3_ios_game_patch_info) == 32);
	static_assert(sizeof(rpcs3_ios_runtime_patch_info) == 80);
	static_assert(sizeof(rpcs3_ios_setting_info) == 96);
	static_assert(sizeof(rpcs3_ios_setting_option) == 32);
	static_assert(sizeof(rpcs3_ios_performance_metrics) == 48);
	static_assert(RPCS3_IOS_PERFORMANCE_FPS_VALID == 1);
	static_assert(RPCS3_IOS_PERFORMANCE_MEMORY_VALID == 8);
	static_assert(RPCS3_IOS_SETTING_BOOLEAN == 0);
	static_assert(RPCS3_IOS_SETTING_TEXT == 4);
	static_assert(RPCS3_IOS_EMULATION_STATE_UNKNOWN == 0);
	static_assert(RPCS3_IOS_EMULATION_STATE_STOPPED == 1);
	static_assert(RPCS3_IOS_EMULATION_STATE_STOPPING == 7);

	assert(validate_config_contract(nullptr) == RPCS3_IOS_INVALID_ARGUMENT);
	rpcs3_ios_config config{};
	config.abi_version = RPCS3_IOS_ABI_VERSION;
	config.struct_size = sizeof(config);
	config.application_support_path = "/tmp/rpcs3-support";
	config.cache_path = "/tmp/rpcs3-cache";
	assert(validate_config_contract(&config) == RPCS3_IOS_OK);
	config.cache_path = "relative";
	assert(validate_config_contract(&config) == RPCS3_IOS_INVALID_ARGUMENT);

	assert(validate_idle_operation_contract(
		RPCS3_IOS_STATE_READY,
		RPCS3_IOS_EMULATION_STATE_STOPPED) == RPCS3_IOS_OK);
	assert(validate_idle_operation_contract(
		RPCS3_IOS_STATE_UNINITIALIZED,
		RPCS3_IOS_EMULATION_STATE_STOPPED) == RPCS3_IOS_INVALID_STATE);
	assert(validate_idle_operation_contract(
		RPCS3_IOS_STATE_READY,
		RPCS3_IOS_EMULATION_STATE_RUNNING) == RPCS3_IOS_INVALID_STATE);
	assert(validate_pause_operation_contract(
		RPCS3_IOS_STATE_READY,
		RPCS3_IOS_EMULATION_STATE_RUNNING) == RPCS3_IOS_OK);
	assert(validate_pause_operation_contract(
		RPCS3_IOS_STATE_READY,
		RPCS3_IOS_EMULATION_STATE_PAUSED) == RPCS3_IOS_INVALID_STATE);
	assert(validate_pause_operation_contract(
		RPCS3_IOS_STATE_UNINITIALIZED,
		RPCS3_IOS_EMULATION_STATE_RUNNING) == RPCS3_IOS_INVALID_STATE);
	assert(validate_resume_operation_contract(
		RPCS3_IOS_STATE_READY,
		RPCS3_IOS_EMULATION_STATE_PAUSED) == RPCS3_IOS_OK);
	assert(validate_resume_operation_contract(
		RPCS3_IOS_STATE_READY,
		RPCS3_IOS_EMULATION_STATE_RUNNING) == RPCS3_IOS_INVALID_STATE);
	assert(validate_resume_operation_contract(
		RPCS3_IOS_STATE_STOPPED,
		RPCS3_IOS_EMULATION_STATE_PAUSED) == RPCS3_IOS_INVALID_STATE);
	config.cache_path = "/tmp/rpcs3-cache";
	config.abi_version++;
	assert(validate_config_contract(&config) == RPCS3_IOS_INVALID_ARGUMENT);
	config.abi_version = RPCS3_IOS_ABI_VERSION;
	config.struct_size = sizeof(config) - 1;
	assert(validate_config_contract(&config) == RPCS3_IOS_INVALID_ARGUMENT);
	config.struct_size = sizeof(config);
	config.application_support_path = "";
	assert(validate_config_contract(&config) == RPCS3_IOS_INVALID_ARGUMENT);
	config.application_support_path = "/tmp/rpcs3-support";
	config.cache_path = nullptr;
	assert(validate_config_contract(&config) == RPCS3_IOS_INVALID_ARGUMENT);

	display_surface_registry surfaces;
	assert(!surfaces.snapshot().valid());
	assert(surfaces.update(nullptr, true) == RPCS3_IOS_OK);
	rpcs3_ios_display_surface surface{};
	surface.struct_size = sizeof(surface);
	surface.width = 2796;
	surface.height = 1290;
	surface.refresh_rate = 120;
	surface.metal_layer = reinterpret_cast<void*>(0x1000);
	assert(validate_display_surface_contract(&surface) == RPCS3_IOS_OK);
	assert(surfaces.update(&surface, true) == RPCS3_IOS_OK);
	assert(surfaces.snapshot().metal_layer == surface.metal_layer);
	assert(surfaces.snapshot().width == 2796);
	surface.width = 2556;
	assert(surfaces.update(&surface, false) == RPCS3_IOS_OK);
	assert(surfaces.snapshot().width == 2556);
	rpcs3_ios_display_surface replacement = surface;
	replacement.metal_layer = reinterpret_cast<void*>(0x2000);
	assert(surfaces.update(&replacement, false) == RPCS3_IOS_INVALID_STATE);
	assert(surfaces.update(nullptr, false) == RPCS3_IOS_INVALID_STATE);
	assert(surfaces.update(&replacement, true) == RPCS3_IOS_OK);
	assert(surfaces.update(nullptr, true) == RPCS3_IOS_OK);
	assert(!surfaces.snapshot().valid());
	surface.width = 0;
	assert(validate_display_surface_contract(&surface) == RPCS3_IOS_INVALID_ARGUMENT);
	surface.width = 2556;
	surface.struct_size = sizeof(surface) - 1;
	assert(validate_display_surface_contract(&surface) == RPCS3_IOS_INVALID_ARGUMENT);

	error_store errors;
	assert(errors.get().empty());
	errors.set("first failure");
	assert(errors.get() == "first failure");
	errors.set("latest failure");
	assert(errors.get() == "latest failure");

	lifecycle state;
	assert(state.state() == RPCS3_IOS_STATE_UNINITIALIZED);
	assert(state.begin_initialize() == RPCS3_IOS_OK);
	assert(state.state() == RPCS3_IOS_STATE_INITIALIZING);
	assert(state.begin_initialize() == RPCS3_IOS_INVALID_STATE);
	state.finish_initialize(false);
	assert(state.state() == RPCS3_IOS_STATE_FAILED);

	bool should_shutdown = false;
	assert(state.begin_shutdown(should_shutdown) == RPCS3_IOS_OK);
	assert(should_shutdown);
	assert(state.state() == RPCS3_IOS_STATE_SHUTTING_DOWN);
	assert(state.begin_shutdown(should_shutdown) == RPCS3_IOS_INVALID_STATE);
	state.finish_shutdown(true);
	assert(state.state() == RPCS3_IOS_STATE_STOPPED);
	assert(state.begin_shutdown(should_shutdown) == RPCS3_IOS_OK);
	assert(!should_shutdown);
	assert(state.begin_initialize() == RPCS3_IOS_INVALID_STATE);

	lifecycle firmware_state;
	assert(firmware_state.begin_initialize() == RPCS3_IOS_OK);
	firmware_state.finish_initialize(true);
	assert(firmware_state.begin_content_install() == RPCS3_IOS_OK);
	assert(firmware_state.begin_content_install() == RPCS3_IOS_INVALID_STATE);
	assert(firmware_state.begin_shutdown(should_shutdown) == RPCS3_IOS_INVALID_STATE);
	firmware_state.finish_content_install();
	assert(firmware_state.state() == RPCS3_IOS_STATE_READY);
	assert(firmware_state.begin_shutdown(should_shutdown) == RPCS3_IOS_OK);
	assert(should_shutdown);

	return 0;
}
