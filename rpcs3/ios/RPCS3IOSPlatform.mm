#include "RPCS3IOSPlatform.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#import <UIKit/UIKit.h>
#pragma clang diagnostic pop

#include <dispatch/dispatch.h>

namespace rpcs3::ios
{
namespace
{
void apply_display_sleep(void* context)
{
	const bool enable = context != nullptr;
	UIApplication.sharedApplication.idleTimerDisabled = !enable;
}
}

bool display_sleep_control_supported() noexcept
{
	return true;
}

void enable_display_sleep(bool enable) noexcept
{
	void* context = enable ? reinterpret_cast<void*>(1) : nullptr;
	if (NSThread.isMainThread)
	{
		apply_display_sleep(context);
		return;
	}

	dispatch_async_f(dispatch_get_main_queue(), context, &apply_display_sleep);
}
}
