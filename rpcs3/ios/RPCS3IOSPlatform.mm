#include "RPCS3IOSPlatform.h"
#include "IOSGraphicsLifecycle.h"
#include "IOSAudioSession.h"
#include "util/logs.hpp"

LOG_CHANNEL(ios_graphics_log, "iOS Graphics");

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
#else
#import <UIKit/UIKit.h>
#endif
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

@interface RPCS3AudioSessionObserver : NSObject
- (void)willDeactivate:(NSNotification*)notification;
- (void)didActivate:(NSNotification*)notification;
@end

@implementation RPCS3AudioSessionObserver
- (void)willDeactivate:(NSNotification*)notification
{
	(void)notification;
	rpcs3::ios::audio_session_state().set_active(false);
}
- (void)didActivate:(NSNotification*)notification
{
	(void)notification;
	rpcs3::ios::audio_session_state().set_active(true);
}
@end

namespace rpcs3::ios
{
audio_session_lifecycle& audio_session_state()
{
	static audio_session_lifecycle state;
	return state;
}

graphics_lifecycle& graphics_lifecycle_state()
{
	static graphics_lifecycle state;
	return state;
}

void initialize_graphics_lifecycle()
{
#if TARGET_OS_OSX
	// macOS permits rendering while another app (including LLDB) has focus.
	graphics_lifecycle_state().set_active(true);
#else
	// Never dispatch synchronously to UIKit while holding the lifecycle ABI lock.
	static std::once_flag once;
	std::call_once(once, []
	{
		// Install synchronously during core initialization, before the wrapper
		// activates its session and before any audio backend can be constructed.
		// Delivery is synchronous on the posting thread; no UIKit calls here.
		static RPCS3AudioSessionObserver* audio_observer = [RPCS3AudioSessionObserver new];
		NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
		[center addObserver:audio_observer selector:@selector(willDeactivate:)
			name:@"RPCS3AudioSessionWillDeactivate" object:nil];
		[center addObserver:audio_observer selector:@selector(didActivate:)
			name:@"RPCS3AudioSessionDidActivate" object:nil];
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
#endif
}

namespace
{
#if !TARGET_OS_VISION
void apply_display_sleep(void* context)
{
	const bool enable = context != nullptr;
#if TARGET_OS_OSX
	static id activity = nil;
	if (enable && activity)
	{
		[NSProcessInfo.processInfo endActivity:activity];
		[activity release];
		activity = nil;
	}
	else if (!enable && !activity)
	{
		activity = [[NSProcessInfo.processInfo
			beginActivityWithOptions:NSActivityIdleDisplaySleepDisabled
			reason:@"RPCS3 emulation"] retain];
	}
#else
	UIApplication.sharedApplication.idleTimerDisabled = !enable;
#endif
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
