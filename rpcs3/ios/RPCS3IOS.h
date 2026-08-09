#ifndef RPCS3_IOS_H
#define RPCS3_IOS_H

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#define RPCS3_IOS_NOEXCEPT noexcept
#else
#define RPCS3_IOS_NOEXCEPT
#endif

#if defined(RPCS3_IOS_CORE_BUILD)
#define RPCS3_IOS_EXPORT __attribute__((visibility("default")))
#else
#define RPCS3_IOS_EXPORT
#endif

#define RPCS3_IOS_ABI_VERSION 2u

typedef enum rpcs3_ios_status
{
    RPCS3_IOS_OK = 0,
    RPCS3_IOS_INVALID_ARGUMENT = 1,
    RPCS3_IOS_INVALID_STATE = 2,
    RPCS3_IOS_JIT_UNAVAILABLE = 3,
    RPCS3_IOS_JIT_MAPPING_FAILED = 4,
    RPCS3_IOS_CORE_INIT_FAILED = 5,
    RPCS3_IOS_SELF_TEST_FAILED = 6,
    RPCS3_IOS_INTERNAL_ERROR = 7,
    RPCS3_IOS_FIRMWARE_INVALID = 8,
    RPCS3_IOS_FIRMWARE_INSTALL_FAILED = 9
} rpcs3_ios_status;

typedef enum rpcs3_ios_state
{
    RPCS3_IOS_STATE_UNINITIALIZED = 0,
    RPCS3_IOS_STATE_INITIALIZING = 1,
    RPCS3_IOS_STATE_READY = 2,
    RPCS3_IOS_STATE_SHUTTING_DOWN = 3,
    RPCS3_IOS_STATE_STOPPED = 4,
    RPCS3_IOS_STATE_FAILED = 5
} rpcs3_ios_state;

typedef void (*rpcs3_ios_log_callback)(void* user_context, int32_t level, const char* message);
typedef void (*rpcs3_ios_firmware_progress_callback)(
    void* user_context,
    uint32_t completed,
    uint32_t total,
    const char* stage);
typedef void (*rpcs3_ios_main_thread_task)(void* task_context);
typedef void (*rpcs3_ios_main_thread_callback)(
    void* user_context,
    rpcs3_ios_main_thread_task task,
    void* task_context);

typedef struct rpcs3_ios_config
{
    uint32_t abi_version;
    uint32_t struct_size;
    const char* application_support_path;
    const char* cache_path;
    rpcs3_ios_log_callback log_callback;
    rpcs3_ios_main_thread_callback main_thread_callback;
    void* user_context;
} rpcs3_ios_config;

RPCS3_IOS_EXPORT uint32_t rpcs3_ios_abi_version(void) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT const char* rpcs3_ios_build_info(void) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_initialize(const rpcs3_ios_config* config) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_run_llvm_self_test(uint64_t input, uint64_t* output) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT const char* rpcs3_ios_firmware_version(void) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_install_firmware(
    const char* pup_path,
    rpcs3_ios_firmware_progress_callback progress_callback,
    void* user_context) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_state rpcs3_ios_get_state(void) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_shutdown(void) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT const char* rpcs3_ios_last_error(void) RPCS3_IOS_NOEXCEPT;

#if defined(__cplusplus)
}
#endif

#undef RPCS3_IOS_NOEXCEPT

#endif
