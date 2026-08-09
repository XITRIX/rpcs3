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

#define RPCS3_IOS_ABI_VERSION 7u

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
    RPCS3_IOS_FIRMWARE_INSTALL_FAILED = 9,
    RPCS3_IOS_BOOT_FAILED = 10,
    RPCS3_IOS_STOP_FAILED = 11,
    RPCS3_IOS_PACKAGE_INVALID = 12,
    RPCS3_IOS_PACKAGE_INSTALL_FAILED = 13,
    RPCS3_IOS_GAME_NOT_FOUND = 14
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

typedef enum rpcs3_ios_emulation_state
{
    RPCS3_IOS_EMULATION_STATE_UNKNOWN = 0,
    RPCS3_IOS_EMULATION_STATE_STOPPED = 1,
    RPCS3_IOS_EMULATION_STATE_LOADING = 2,
    RPCS3_IOS_EMULATION_STATE_READY = 3,
    RPCS3_IOS_EMULATION_STATE_STARTING = 4,
    RPCS3_IOS_EMULATION_STATE_RUNNING = 5,
    RPCS3_IOS_EMULATION_STATE_PAUSED = 6,
    RPCS3_IOS_EMULATION_STATE_STOPPING = 7
} rpcs3_ios_emulation_state;

typedef void (*rpcs3_ios_log_callback)(void* user_context, int32_t level, const char* message);
typedef void (*rpcs3_ios_firmware_progress_callback)(
    void* user_context,
    uint32_t completed,
    uint32_t total,
    const char* stage);
typedef void (*rpcs3_ios_package_progress_callback)(
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

// The wrapper owns metal_layer for the complete attached lifetime. Width and
// height are the CAMetalLayer drawable size in physical pixels.
typedef struct rpcs3_ios_display_surface
{
    uint32_t struct_size;
    uint32_t width;
    uint32_t height;
    float refresh_rate;
    void* metal_layer;
} rpcs3_ios_display_surface;

typedef enum rpcs3_ios_pad_button
{
    RPCS3_IOS_PAD_BUTTON_DPAD_UP = UINT64_C(1) << 0,
    RPCS3_IOS_PAD_BUTTON_DPAD_DOWN = UINT64_C(1) << 1,
    RPCS3_IOS_PAD_BUTTON_DPAD_LEFT = UINT64_C(1) << 2,
    RPCS3_IOS_PAD_BUTTON_DPAD_RIGHT = UINT64_C(1) << 3,
    RPCS3_IOS_PAD_BUTTON_CROSS = UINT64_C(1) << 4,
    RPCS3_IOS_PAD_BUTTON_CIRCLE = UINT64_C(1) << 5,
    RPCS3_IOS_PAD_BUTTON_SQUARE = UINT64_C(1) << 6,
    RPCS3_IOS_PAD_BUTTON_TRIANGLE = UINT64_C(1) << 7,
    RPCS3_IOS_PAD_BUTTON_L1 = UINT64_C(1) << 8,
    RPCS3_IOS_PAD_BUTTON_R1 = UINT64_C(1) << 9,
    RPCS3_IOS_PAD_BUTTON_L2 = UINT64_C(1) << 10,
    RPCS3_IOS_PAD_BUTTON_R2 = UINT64_C(1) << 11,
    RPCS3_IOS_PAD_BUTTON_L3 = UINT64_C(1) << 12,
    RPCS3_IOS_PAD_BUTTON_R3 = UINT64_C(1) << 13,
    RPCS3_IOS_PAD_BUTTON_START = UINT64_C(1) << 14,
    RPCS3_IOS_PAD_BUTTON_SELECT = UINT64_C(1) << 15,
    RPCS3_IOS_PAD_BUTTON_PS = UINT64_C(1) << 16
} rpcs3_ios_pad_button;

// A replaceable host-input snapshot for player one. Axes use [-1, 1], with
// positive Y pointing up; triggers use [0, 1]. The core copies this structure
// synchronously, so the caller may release it as soon as the function returns.
typedef struct rpcs3_ios_pad_state
{
    uint32_t struct_size;
    uint32_t connected;
    uint64_t buttons;
    float left_stick_x;
    float left_stick_y;
    float right_stick_x;
    float right_stick_y;
    float left_trigger;
    float right_trigger;
} rpcs3_ios_pad_state;

// Strings are UTF-8 and remain valid only for the duration of the synchronous
// enumeration callback. icon_path is empty when the title has no ICON0.PNG.
typedef struct rpcs3_ios_game_info
{
    uint32_t struct_size;
    const char* title_id;
    const char* title;
    const char* version;
    const char* category;
    const char* icon_path;
} rpcs3_ios_game_info;

typedef void (*rpcs3_ios_game_callback)(
    void* user_context,
    const rpcs3_ios_game_info* game);

RPCS3_IOS_EXPORT uint32_t rpcs3_ios_abi_version(void) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT const char* rpcs3_ios_build_info(void) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_initialize(const rpcs3_ios_config* config) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_run_llvm_self_test(uint64_t input, uint64_t* output) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT const char* rpcs3_ios_firmware_version(void) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_install_firmware(
    const char* pup_path,
    rpcs3_ios_firmware_progress_callback progress_callback,
    void* user_context) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_install_package(
    const char* package_path,
    rpcs3_ios_package_progress_callback progress_callback,
    void* user_context) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_enumerate_games(
    rpcs3_ios_game_callback callback,
    void* user_context) RPCS3_IOS_NOEXCEPT;
// Pass NULL to detach. Detach and layer replacement require stopped
// emulation; dimensions for the currently attached layer may change live.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_set_display_surface(
    const rpcs3_ios_display_surface* surface) RPCS3_IOS_NOEXCEPT;
// This setter is safe while boot/compilation owns the serialized lifecycle
// lock. Pass connected=0 to release every input and disconnect player one.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_set_pad_state(
    const rpcs3_ios_pad_state* state) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_boot_vsh(void) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_boot_game(
    const char* title_id) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_emulation_state rpcs3_ios_get_emulation_state(void) RPCS3_IOS_NOEXCEPT;
// Observational and safe to poll while boot is running. A zero total means
// RPCS3 knows the current stage but cannot calculate a percentage yet.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_get_boot_progress(
    uint32_t* completed,
    uint32_t* total,
    char* stage,
    size_t stage_capacity) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_stop_emulation(void) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_state rpcs3_ios_get_state(void) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_shutdown(void) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT const char* rpcs3_ios_last_error(void) RPCS3_IOS_NOEXCEPT;

#if defined(__cplusplus)
}
#endif

#undef RPCS3_IOS_NOEXCEPT

#endif
