#include "RPCS3IOSPlatform.h"
#include "IOSGraphicsLifecycle.h"
#include "util/logs.hpp"

LOG_CHANNEL(ios_graphics_log, "iOS Graphics");

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#import <UIKit/UIKit.h>
#pragma clang diagnostic pop

#include <TargetConditionals.h>

#include <dispatch/dispatch.h>

@interface RPCS3GraphicsLifecycleObserver : NSObject
- (void)willResignActive:(NSNotification*)notification;
- (void)didBecomeActive:(NSNotification*)notification;
@end

@implementation RPCS3GraphicsLifecycleObserver
- (void)willResignActive:(NSNotification*)notification
{
	(void)notification;
	rpcs3::ios::graphics_lifecycle_state().set_active(false);
	ios_graphics_log.notice("Vulkan submissions suspended and GPU work drained before backgrounding");
}
- (void)didBecomeActive:(NSNotification*)notification
{
	(void)notification;
	rpcs3::ios::graphics_lifecycle_state().set_active(true);
	ios_graphics_log.notice("Vulkan submissions resumed; next frame will refresh the swapchain");
}
@end

namespace rpcs3::ios
{
graphics_lifecycle& graphics_lifecycle_state()
{
	static graphics_lifecycle state;
	return state;
}

void initialize_graphics_lifecycle()
{
	// Never dispatch synchronously to UIKit while holding the lifecycle ABI lock.
	static std::once_flag once;
	std::call_once(once, []
	{
		dispatch_async(dispatch_get_main_queue(), ^
		{
			static RPCS3GraphicsLifecycleObserver* observer = [RPCS3GraphicsLifecycleObserver new];
			NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
			[center addObserver:observer selector:@selector(willResignActive:)
				name:UIApplicationWillResignActiveNotification object:nil];
			[center addObserver:observer selector:@selector(didBecomeActive:)
				name:UIApplicationDidBecomeActiveNotification object:nil];
			graphics_lifecycle_state().set_active(UIApplication.sharedApplication.applicationState == UIApplicationStateActive);
		});
	});
}

namespace
{
#if !TARGET_OS_VISION
void apply_display_sleep(void* context)
{
	const bool enable = context != nullptr;
	UIApplication.sharedApplication.idleTimerDisabled = !enable;
}
#endif
}

bool display_sleep_control_supported() noexcept
{
#if TARGET_OS_VISION
	return false;
#else
	return true;
#endif
}

void enable_display_sleep(bool enable) noexcept
{
#if TARGET_OS_VISION
	(void)enable;
#else
	void* context = enable ? reinterpret_cast<void*>(1) : nullptr;
	if (NSThread.isMainThread)
	{
		apply_display_sleep(context);
		return;
	}

	dispatch_async_f(dispatch_get_main_queue(), context, &apply_display_sleep);
#endif
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

std::string localized_application_string(
	std::string_view language_tag,
	std::string_view localization_key,
	std::string_view english_value)
{
	@autoreleasepool
	{
		// NSBundle owns locale negotiation, including the user's per-app language.
		// Keep the explicit tag in this boundary so deterministic/test resolvers and
		// a future runtime language selector do not require a core ABI change.
		(void)language_tag;
		NSString* key = [[NSString alloc]
			initWithBytes:localization_key.data()
			length:localization_key.size()
			encoding:NSUTF8StringEncoding];
		if (!key)
		{
			return std::string{english_value};
		}
		NSString* fallback = [[NSString alloc]
			initWithBytes:english_value.data()
			length:english_value.size()
			encoding:NSUTF8StringEncoding];
		if (!fallback)
		{
			return std::string{english_value};
		}
		NSString* localized = [NSBundle.mainBundle
			localizedStringForKey:key
			value:fallback
			table:@"RPCS3Core"];
		return localized.UTF8String ? std::string{localized.UTF8String} : std::string{english_value};
	}
}
}
