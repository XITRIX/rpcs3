#include "stdafx.h"
#include "RPCS3IOSCapabilities.h"
#include "Emu/NP/upnp_handler.h"
#include "util/logs.hpp"

LOG_CHANNEL(upnp_log, "UPNP");

static_assert(!rpcs3::ios::capabilities::upnp);

upnp_handler::~upnp_handler() = default;

void upnp_handler::upnp_enable()
{
	upnp_log.notice("UPnP is unavailable in the iOS frontend");
}

void upnp_handler::add_port_redir(const std::string&, u16, std::string_view)
{
}

void upnp_handler::remove_port_redir(u16, std::string_view)
{
}

void upnp_handler::remove_port_redir_external(u16, std::string_view, bool)
{
}

bool upnp_handler::is_active() const
{
	return false;
}
