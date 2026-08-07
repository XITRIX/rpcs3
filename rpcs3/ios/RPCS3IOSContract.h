#pragma once

#include "RPCS3IOS.h"

namespace rpcs3::ios
{
inline rpcs3_ios_status validate_config_contract(const rpcs3_ios_config* config) noexcept
{
	if (!config || config->abi_version != RPCS3_IOS_ABI_VERSION ||
		config->struct_size < sizeof(rpcs3_ios_config) ||
		!config->application_support_path || !config->cache_path ||
		config->application_support_path[0] != '/' || config->cache_path[0] != '/')
	{
		return RPCS3_IOS_INVALID_ARGUMENT;
	}

	return RPCS3_IOS_OK;
}

class lifecycle final
{
public:
	rpcs3_ios_state state() const noexcept
	{
		return m_state;
	}

	rpcs3_ios_status begin_initialize() noexcept
	{
		if (m_state != RPCS3_IOS_STATE_UNINITIALIZED)
		{
			return RPCS3_IOS_INVALID_STATE;
		}

		m_state = RPCS3_IOS_STATE_INITIALIZING;
		return RPCS3_IOS_OK;
	}

	void finish_initialize(bool success) noexcept
	{
		m_state = success ? RPCS3_IOS_STATE_READY : RPCS3_IOS_STATE_FAILED;
	}

	rpcs3_ios_status begin_shutdown(bool& should_run) noexcept
	{
		should_run = false;
		if (m_state == RPCS3_IOS_STATE_UNINITIALIZED || m_state == RPCS3_IOS_STATE_STOPPED)
		{
			return RPCS3_IOS_OK;
		}
		if (m_state == RPCS3_IOS_STATE_INITIALIZING || m_state == RPCS3_IOS_STATE_SHUTTING_DOWN)
		{
			return RPCS3_IOS_INVALID_STATE;
		}

		m_state = RPCS3_IOS_STATE_SHUTTING_DOWN;
		should_run = true;
		return RPCS3_IOS_OK;
	}

	void finish_shutdown(bool success) noexcept
	{
		m_state = success ? RPCS3_IOS_STATE_STOPPED : RPCS3_IOS_STATE_FAILED;
	}

private:
	rpcs3_ios_state m_state = RPCS3_IOS_STATE_UNINITIALIZED;
};
}
