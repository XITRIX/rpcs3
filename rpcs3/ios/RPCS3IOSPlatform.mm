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

std::string preferred_language_identifier()
{
	@autoreleasepool
	{
		NSString* language = NSBundle.mainBundle.preferredLocalizations.firstObject;
		if (!language.length)
		{
			language = NSLocale.preferredLanguages.firstObject;
		}
		if (!language.length)
		{
			return "en";
		}
		return language.UTF8String ?: "en";
	}
}

std::string localized_application_string(std::string_view language_tag, std::string_view english_value)
{
	@autoreleasepool
	{
		// NSBundle owns locale negotiation, including the user's per-app language.
		// Keep the explicit tag in this boundary so deterministic/test resolvers and
		// a future runtime language selector do not require a core ABI change.
		(void)language_tag;
		NSString* key = [[NSString alloc]
			initWithBytes:english_value.data()
			length:english_value.size()
			encoding:NSUTF8StringEncoding];
		if (!key)
		{
			return std::string{english_value};
		}
		NSString* localized = [NSBundle.mainBundle
			localizedStringForKey:key
			value:key
			table:@"RPCS3Core"];
		return localized.UTF8String ? std::string{localized.UTF8String} : std::string{english_value};
	}
}
}
