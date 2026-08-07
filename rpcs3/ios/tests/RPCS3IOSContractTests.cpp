#include "../RPCS3IOSContract.h"

#include <cassert>

int main()
{
	using namespace rpcs3::ios;

	assert(validate_config_contract(nullptr) == RPCS3_IOS_INVALID_ARGUMENT);
	rpcs3_ios_config config{};
	config.abi_version = RPCS3_IOS_ABI_VERSION;
	config.struct_size = sizeof(config);
	config.application_support_path = "/tmp/rpcs3-support";
	config.cache_path = "/tmp/rpcs3-cache";
	assert(validate_config_contract(&config) == RPCS3_IOS_OK);
	config.cache_path = "relative";
	assert(validate_config_contract(&config) == RPCS3_IOS_INVALID_ARGUMENT);
	config.cache_path = "/tmp/rpcs3-cache";
	config.abi_version++;
	assert(validate_config_contract(&config) == RPCS3_IOS_INVALID_ARGUMENT);

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

	return 0;
}
