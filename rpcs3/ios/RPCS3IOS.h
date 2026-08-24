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

#define RPCS3_IOS_ABI_VERSION 28u

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
    RPCS3_IOS_GAME_NOT_FOUND = 14,
    RPCS3_IOS_ISO_INVALID = 15,
    RPCS3_IOS_ISO_INSTALL_FAILED = 16,
    RPCS3_IOS_ZIP_INVALID = 17,
    RPCS3_IOS_ZIP_INSTALL_FAILED = 18,
    RPCS3_IOS_SETTING_NOT_FOUND = 19,
    RPCS3_IOS_SETTING_INVALID = 20,
    RPCS3_IOS_SETTINGS_SAVE_FAILED = 21,
    RPCS3_IOS_FOLDER_INVALID = 22,
    RPCS3_IOS_FOLDER_INSTALL_FAILED = 23,
    RPCS3_IOS_PATCH_INVALID = 24,
    RPCS3_IOS_PATCH_TITLE_MISMATCH = 25,
    RPCS3_IOS_PATCH_INSTALL_FAILED = 26,
    RPCS3_IOS_PATCH_REPOSITORY_INVALID = 27,
    RPCS3_IOS_PATCH_REPOSITORY_INSTALL_FAILED = 28,
    RPCS3_IOS_RUNTIME_PATCH_NOT_FOUND = 29,
    RPCS3_IOS_RUNTIME_PATCH_SAVE_FAILED = 30,
    RPCS3_IOS_RPCN_ERROR = 31,
    RPCS3_IOS_RPCN_NOT_CONFIGURED = 32,
    RPCS3_IOS_RPCN_SERVER_EXISTS = 33,
    RPCS3_IOS_RPCN_SERVER_NOT_FOUND = 34,
    RPCS3_IOS_RAP_INVALID = 35,
    RPCS3_IOS_RAP_INSTALL_FAILED = 36,
    RPCS3_IOS_NETWORK_ERROR = 37,
    RPCS3_IOS_RESPONSE_TOO_LARGE = 38,
    RPCS3_IOS_GAME_DELETE_FAILED = 39,
    RPCS3_IOS_CONFIG_DATABASE_INVALID = 40,
    RPCS3_IOS_CONFIG_DATABASE_STORAGE_FAILED = 41,
    RPCS3_IOS_SETTINGS_PRESET_INVALID = 42,
    RPCS3_IOS_SETTINGS_PRESET_EXISTS = 43,
    RPCS3_IOS_SETTINGS_PRESET_NOT_FOUND = 44,
    RPCS3_IOS_GAME_CACHE_FAILED = 45
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
typedef void (*rpcs3_ios_iso_progress_callback)(
    void* user_context,
    uint32_t completed,
    uint32_t total,
    const char* stage);
typedef void (*rpcs3_ios_zip_progress_callback)(
    void* user_context,
    uint32_t completed,
    uint32_t total,
    const char* stage);
typedef void (*rpcs3_ios_folder_progress_callback)(
    void* user_context,
    uint32_t completed,
    uint32_t total,
    const char* stage);
typedef void (*rpcs3_ios_download_progress_callback)(
    void* user_context,
    uint64_t completed,
    uint64_t total);
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

// A replaceable host-input snapshot for one player. Axes use [-1, 1], with
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

// A lock-independent per-player output snapshot. Motor values use RPCS3's
// adjusted [0, 255] range after the active pad configuration's threshold,
// multiplier, and motor-swap settings have been applied.
typedef struct rpcs3_ios_pad_feedback
{
    uint32_t struct_size;
    uint32_t large_motor;
    uint32_t small_motor;
    uint32_t reserved;
} rpcs3_ios_pad_feedback;

// Strings are UTF-8 and remain valid only for the duration of the synchronous
// enumeration callback. icon_path is empty when the title has no ICON0.PNG.
// size_on_disk is UINT64_MAX when the size could not be determined. New fields
// are appended so ABI v24 clients can use struct_size for forward compatibility.
typedef struct rpcs3_ios_game_info
{
    uint32_t struct_size;
    const char* title_id;
    const char* title;
    const char* version;
    const char* category;
    const char* icon_path;
    const char* firmware_version;
    const char* path;
    uint32_t attribute;
    uint32_t bootable;
    uint32_t parental_level;
    uint32_t sound_format;
    uint32_t resolution;
    uint32_t reserved;
    uint64_t size_on_disk;
} rpcs3_ios_game_info;

typedef void (*rpcs3_ios_game_callback)(
    void* user_context,
    const rpcs3_ios_game_info* game);

typedef enum rpcs3_ios_game_cache_type
{
    RPCS3_IOS_GAME_CACHE_SHADER = 1,
    RPCS3_IOS_GAME_CACHE_PPU = 2,
    RPCS3_IOS_GAME_CACHE_SPU = 3,
    RPCS3_IOS_GAME_CACHE_HDD1 = 4,
    RPCS3_IOS_GAME_CACHE_ALL = 5
} rpcs3_ios_game_cache_type;

typedef struct rpcs3_ios_game_cache_info
{
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t shader;
    uint64_t ppu;
    uint64_t spu;
    uint64_t hdd1;
    uint64_t total;
} rpcs3_ios_game_cache_info;

typedef enum rpcs3_ios_trophy_grade
{
    RPCS3_IOS_TROPHY_GRADE_UNKNOWN = 0,
    RPCS3_IOS_TROPHY_GRADE_PLATINUM = 1,
    RPCS3_IOS_TROPHY_GRADE_GOLD = 2,
    RPCS3_IOS_TROPHY_GRADE_SILVER = 3,
    RPCS3_IOS_TROPHY_GRADE_BRONZE = 4
} rpcs3_ios_trophy_grade;

// Strings and icon_path remain valid only for the duration of the callback.
// unlock_timestamp is the PS3 RTC value stored in TROPUSR.DAT, in microseconds
// since 0001-01-01. hidden is metadata, independent of earned state.
typedef struct rpcs3_ios_trophy_info
{
    uint32_t struct_size;
    uint32_t trophy_id;
    uint32_t display_order;
    uint32_t grade;
    uint32_t earned;
    uint32_t hidden;
    uint32_t reserved0;
    uint32_t reserved1;
    uint64_t unlock_timestamp;
    const char* trophy_set_id;
    const char* game_title;
    const char* name;
    const char* description;
    const char* icon_path;
} rpcs3_ios_trophy_info;

typedef void (*rpcs3_ios_trophy_callback)(
    void* user_context,
    const rpcs3_ios_trophy_info* trophy);

// PS3 update packages are cumulative in dev_hdd0/game, so enumeration reports
// the currently installed update state for the requested title ID. The ABI
// keeps the internal "patch" name because PS3 PKG metadata calls it a patch.
typedef struct rpcs3_ios_game_patch_info
{
    uint32_t struct_size;
    const char* title_id;
    const char* title;
    const char* version;
} rpcs3_ios_game_patch_info;

typedef void (*rpcs3_ios_game_patch_callback)(
    void* user_context,
    const rpcs3_ios_game_patch_info* patch);

// RPCS3 Patch Engine entries are distinct from installed PS3 update PKGs.
// These callback-scoped strings describe one patch variant that targets the
// requested title and either its exact application version or all versions.
typedef struct rpcs3_ios_runtime_patch_info
{
    uint32_t struct_size;
    uint32_t enabled;
    uint32_t configurable_count;
    uint32_t reserved;
    const char* hash;
    const char* title;
    const char* description;
    const char* patch_version;
    const char* author;
    const char* notes;
    const char* patch_group;
    const char* app_version;
} rpcs3_ios_runtime_patch_info;

typedef void (*rpcs3_ios_runtime_patch_callback)(
    void* user_context,
    const rpcs3_ios_runtime_patch_info* patch);

typedef enum rpcs3_ios_setting_kind
{
    RPCS3_IOS_SETTING_BOOLEAN = 0,
    RPCS3_IOS_SETTING_INTEGER = 1,
    RPCS3_IOS_SETTING_DECIMAL = 2,
    RPCS3_IOS_SETTING_CHOICE = 3,
    RPCS3_IOS_SETTING_TEXT = 4
} rpcs3_ios_setting_kind;

// Strings and option arrays remain valid only for the duration of the
// synchronous enumeration callbacks. Every catalog entry is an allow-listed
// scalar from RPCS3's cfg_root; platform-fixed backends are never exposed.
// recommended_value is non-null only during per-game enumeration when the
// title database changes this setting from RPCS3's built-in default.
typedef struct rpcs3_ios_setting_info
{
    uint32_t struct_size;
    uint32_t kind;
    const char* key;
    const char* category;
    const char* section;
    const char* name;
    const char* description;
    const char* value;
    const char* default_value;
    double minimum;
    double maximum;
    double step;
    uint32_t option_count;
    uint32_t flags;
    const char* recommended_value;
} rpcs3_ios_setting_info;

typedef struct rpcs3_ios_setting_option
{
    uint32_t struct_size;
    uint32_t reserved;
    const char* setting_key;
    const char* value;
    const char* label;
} rpcs3_ios_setting_option;

typedef void (*rpcs3_ios_setting_callback)(
    void* user_context,
    const rpcs3_ios_setting_info* setting);
typedef void (*rpcs3_ios_setting_option_callback)(
    void* user_context,
    const rpcs3_ios_setting_option* option);

// Names are UTF-8 and callback-scoped. modified_time is a Unix timestamp.
// Presets remain separate from custom_configs/config_<TITLE_ID>.yml until
// explicitly applied.
typedef struct rpcs3_ios_game_settings_preset_info
{
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t size;
    int64_t modified_time;
    const char* name;
} rpcs3_ios_game_settings_preset_info;

typedef void (*rpcs3_ios_game_settings_preset_callback)(
    void* user_context,
    const rpcs3_ios_game_settings_preset_info* preset);

// RPCN passwords never leave the core after being submitted. The iOS wrapper
// persists its copy in Keychain, while rpcn.yml retains only public profile and
// server-list data. Strings below are valid only during their callback.
typedef struct rpcs3_ios_rpcn_config_info
{
    uint32_t struct_size;
    uint32_t has_password;
    uint32_t has_token;
    uint32_t ipv6_support;
    uint32_t connected;
    uint32_t authenticated;
    const char* username;
    const char* host;
    const char* online_name;
    const char* avatar_url;
} rpcs3_ios_rpcn_config_info;

typedef void (*rpcs3_ios_rpcn_config_callback)(
    void* user_context,
    const rpcs3_ios_rpcn_config_info* config);

typedef struct rpcs3_ios_rpcn_server_info
{
    uint32_t struct_size;
    uint32_t selected;
    uint32_t removable;
    uint32_t reserved;
    const char* description;
    const char* host;
} rpcs3_ios_rpcn_server_info;

typedef void (*rpcs3_ios_rpcn_server_callback)(
    void* user_context,
    const rpcs3_ios_rpcn_server_info* server);

typedef enum rpcs3_ios_rpcn_social_kind
{
    RPCS3_IOS_RPCN_SOCIAL_FRIEND = 0,
    RPCS3_IOS_RPCN_SOCIAL_REQUEST_RECEIVED = 1,
    RPCS3_IOS_RPCN_SOCIAL_REQUEST_SENT = 2,
    RPCS3_IOS_RPCN_SOCIAL_BLOCKED = 3,
    RPCS3_IOS_RPCN_SOCIAL_RECENT_PLAYER = 4
} rpcs3_ios_rpcn_social_kind;

typedef struct rpcs3_ios_rpcn_social_info
{
    uint32_t struct_size;
    uint32_t kind;
    uint32_t online;
    uint32_t reserved;
    uint64_t timestamp;
    const char* username;
    const char* presence_title;
    const char* presence_status;
    const char* presence_comment;
    const char* history_description;
} rpcs3_ios_rpcn_social_info;

typedef void (*rpcs3_ios_rpcn_social_callback)(
    void* user_context,
    const rpcs3_ios_rpcn_social_info* entry);

typedef enum rpcs3_ios_rpcn_social_action
{
    RPCS3_IOS_RPCN_SOCIAL_ADD_FRIEND = 0,
    RPCS3_IOS_RPCN_SOCIAL_REMOVE_FRIEND = 1,
    RPCS3_IOS_RPCN_SOCIAL_ACCEPT_REQUEST = 2,
    RPCS3_IOS_RPCN_SOCIAL_REJECT_REQUEST = 3,
    RPCS3_IOS_RPCN_SOCIAL_CANCEL_REQUEST = 4,
    RPCS3_IOS_RPCN_SOCIAL_BLOCK_USER = 5,
    RPCS3_IOS_RPCN_SOCIAL_UNBLOCK_USER = 6
} rpcs3_ios_rpcn_social_action;

typedef enum rpcs3_ios_performance_metric_validity
{
    RPCS3_IOS_PERFORMANCE_FPS_VALID = 1u << 0,
    RPCS3_IOS_PERFORMANCE_CPU_VALID = 1u << 1,
    RPCS3_IOS_PERFORMANCE_GPU_VALID = 1u << 2,
    RPCS3_IOS_PERFORMANCE_MEMORY_VALID = 1u << 3
} rpcs3_ios_performance_metric_validity;

// CPU is this process's usage normalized across the device's logical cores.
// GPU is RPCS3's approximate RSX renderer utilization rather than a private
// Metal hardware counter. Memory is this process's physical footprint relative
// to total device memory, not the current iOS jetsam limit.
typedef struct rpcs3_ios_performance_metrics
{
    uint32_t struct_size;
    uint32_t valid_fields;
    double frames_per_second;
    double cpu_usage_percent;
    double gpu_usage_percent;
    uint64_t memory_used_bytes;
    uint64_t memory_total_bytes;
} rpcs3_ios_performance_metrics;

RPCS3_IOS_EXPORT uint32_t rpcs3_ios_abi_version(void) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT const char* rpcs3_ios_build_info(void) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_initialize(const rpcs3_ios_config* config) RPCS3_IOS_NOEXCEPT;
// Validates an opaque response from RPCS3's official configuration endpoint,
// atomically caches it, and publishes title lookups used by Emulator::Load.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_update_config_database(
    const void* content,
    size_t content_size) RPCS3_IOS_NOEXCEPT;
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
// Installs a RAP license for the active PS3 user. The source filename is
// retained as the content ID and its extension is normalized to lowercase.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_install_rap(
    const char* rap_path) RPCS3_IOS_NOEXCEPT;
// key_path may be NULL for decrypted and self-keyed images. Redump images
// require their matching .dkey or .key path to be supplied by the wrapper.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_install_iso(
    const char* iso_path,
    const char* key_path,
    rpcs3_ios_iso_progress_callback progress_callback,
    void* user_context) RPCS3_IOS_NOEXCEPT;
// Installs exactly one extracted PS3 game folder from a non-encrypted ZIP.
// The game may be at the archive root or inside a wrapper directory.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_install_zip(
    const char* zip_path,
    rpcs3_ios_zip_progress_callback progress_callback,
    void* user_context) RPCS3_IOS_NOEXCEPT;
// Copies one security-scoped PS3 game folder into RPCS3's private library.
// The wrapper may remove the source after this operation succeeds to offer
// move semantics while retaining file-provider coordination.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_install_folder(
    const char* folder_path,
    rpcs3_ios_folder_progress_callback progress_callback,
    void* user_context) RPCS3_IOS_NOEXCEPT;
// Installs only a CATEGORY=GD package carrying the PS3 patch flag, and only
// when its TITLE_ID matches expected_title_id.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_install_game_patch(
    const char* expected_title_id,
    const char* package_path,
    rpcs3_ios_package_progress_callback progress_callback,
    void* user_context) RPCS3_IOS_NOEXCEPT;
// Fetches Sony's bounded title-update XML through the core's curl/wolfSSL
// transport because current Apple TLS rejects that legacy endpoint before a
// URLSession trust override can take effect. manifest_size receives raw bytes;
// no terminator is written or included.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_fetch_game_update_manifest(
    const char* title_id,
    void* manifest,
    size_t manifest_capacity,
    size_t* manifest_size) RPCS3_IOS_NOEXCEPT;
// Downloads one manifest-selected package through the same legacy-compatible
// transport. destination_path must be a .pkg below the configured cache root;
// the file is committed atomically only after exactly expected_size bytes.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_download_game_update_package(
    const char* package_url,
    const char* destination_path,
    uint64_t expected_size,
    rpcs3_ios_download_progress_callback progress_callback,
    void* user_context) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_enumerate_games(
    rpcs3_ios_game_callback callback,
    void* user_context) RPCS3_IOS_NOEXCEPT;
// Enumerates registered trophy data for one installed title and the active PS3
// user. The read-only path never generates or repairs TROPUSR.DAT.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_enumerate_trophies(
    const char* title_id,
    rpcs3_ios_trophy_callback callback,
    void* user_context) RPCS3_IOS_NOEXCEPT;
// Permanently deletes the selected title's private base installation,
// associated game/update data, caches, and custom configuration. Save data
// and savestates are retained intentionally.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_delete_game(
    const char* title_id) RPCS3_IOS_NOEXCEPT;
// Reports title-scoped cache usage using RPCS3's native cache layout. The
// operation is available only while emulation is stopped.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_get_game_cache_info(
    const char* title_id,
    rpcs3_ios_game_cache_info* cache_info) RPCS3_IOS_NOEXCEPT;
// Clears one cache category or every title-scoped cache. bytes_removed is the
// measured pre-deletion size and remains an estimate of reclaimed disk space.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_clear_game_cache(
    const char* title_id,
    rpcs3_ios_game_cache_type cache_type,
    uint64_t* bytes_removed) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_enumerate_game_patches(
    const char* title_id,
    rpcs3_ios_game_patch_callback callback,
    void* user_context) RPCS3_IOS_NOEXCEPT;
// Builds the official RPCS3 Patch Engine 1.2 endpoint and includes the local
// patch.yml SHA-256 when present, matching the desktop conditional request.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_get_patch_repository_url(
    char* url,
    size_t url_capacity) RPCS3_IOS_NOEXCEPT;
// The wrapper performs HTTPS and JSON decoding. The core independently checks
// the version and SHA-256, validates the Patch Engine YAML, keeps patch.yml.old,
// and atomically replaces patch.yml only after every check succeeds.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_install_patch_repository(
    const char* version,
    const char* sha256,
    const void* patch_content,
    size_t patch_content_size) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_enumerate_runtime_patches(
    const char* title_id,
    const char* app_version,
    rpcs3_ios_runtime_patch_callback callback,
    void* user_context) RPCS3_IOS_NOEXCEPT;
// Patch state is persisted to patch_config.yml and takes effect on the next
// boot. Enabling an entry disables peers in the same Patch Engine group.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_set_runtime_patch_enabled(
    const char* title_id,
    const char* hash,
    const char* title,
    const char* app_version,
    const char* description,
    uint32_t enabled) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_enumerate_settings(
    rpcs3_ios_setting_callback setting_callback,
    rpcs3_ios_setting_option_callback option_callback,
    void* user_context) RPCS3_IOS_NOEXCEPT;
// Settings are persisted to RPCS3's global config.yml. Mutations require an
// initialized core with emulation stopped and take effect on the next boot.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_set_setting(
    const char* key,
    const char* value) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_reset_settings(void) RPCS3_IOS_NOEXCEPT;
// Per-game settings use RPCS3's desktop-compatible
// custom_configs/config_<TITLE_ID>.yml files. Enumeration reports the
// effective global-plus-custom values, whether a custom file exists, and
// title-database recommendations without exposing YAML to the wrapper.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_enumerate_game_settings(
    const char* title_id,
    rpcs3_ios_setting_callback setting_callback,
    rpcs3_ios_setting_option_callback option_callback,
    void* user_context,
    uint32_t* has_custom_config) RPCS3_IOS_NOEXCEPT;
// The first mutation creates a complete custom configuration from the
// title's current effective settings. Changes take effect on the next boot.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_set_game_setting(
    const char* title_id,
    const char* key,
    const char* value) RPCS3_IOS_NOEXCEPT;
// Restores the audited settings catalog to RPCS3 defaults while retaining a
// custom configuration for the title.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_reset_game_settings(
    const char* title_id) RPCS3_IOS_NOEXCEPT;
// Removes the title's custom configuration so it inherits global settings.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_remove_game_settings(
    const char* title_id) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_enumerate_game_settings_presets(
    const char* title_id,
    rpcs3_ios_game_settings_preset_callback callback,
    void* user_context) RPCS3_IOS_NOEXCEPT;
// Saves the current effective configuration as a new named preset without
// changing the active custom configuration.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_save_game_settings_preset(
    const char* title_id,
    const char* name) RPCS3_IOS_NOEXCEPT;
// Applying a preset atomically replaces the title's active custom config.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_apply_game_settings_preset(
    const char* title_id,
    const char* name) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_duplicate_game_settings_preset(
    const char* title_id,
    const char* source_name,
    const char* destination_name) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_rename_game_settings_preset(
    const char* title_id,
    const char* source_name,
    const char* destination_name) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_delete_game_settings_preset(
    const char* title_id,
    const char* name) RPCS3_IOS_NOEXCEPT;
// Import/export paths must be .yml files below the configured cache root.
// The core bounds and validates imported YAML and never exposes YAML to Swift.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_import_game_settings_preset(
    const char* title_id,
    const char* source_path,
    const char* name) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_export_game_settings_preset(
    const char* title_id,
    const char* name,
    const char* destination_path) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_get_rpcn_config(
    rpcs3_ios_rpcn_config_callback callback,
    void* user_context) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_enumerate_rpcn_servers(
    rpcs3_ios_rpcn_server_callback callback,
    void* user_context) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_set_rpcn_server(
    const char* host) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_add_rpcn_server(
    const char* description,
    const char* host) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_remove_rpcn_server(
    const char* description,
    const char* host) RPCS3_IOS_NOEXCEPT;
// password is the user's original password and is synchronously transformed
// with RPCN's PBKDF2-SHA3-256 contract before this call returns.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_set_rpcn_credentials(
    const char* username,
    const char* password,
    const char* token,
    uint32_t ipv6_support) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_create_rpcn_account(
    const char* username,
    const char* password,
    const char* email) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_test_rpcn_account(void) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_resend_rpcn_token(void) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_request_rpcn_password_reset(
    const char* username,
    const char* email) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_reset_rpcn_password(
    const char* username,
    const char* reset_token,
    const char* new_password) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_delete_rpcn_account(void) RPCS3_IOS_NOEXCEPT;
// Social management is available while a title is running and reuses the
// same authenticated RPCN client as guest matchmaking, presence, scores,
// messaging, TUS, signaling, and clan operations.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_enumerate_rpcn_social(
    rpcs3_ios_rpcn_social_callback callback,
    void* user_context) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_perform_rpcn_social_action(
    uint32_t action,
    const char* username) RPCS3_IOS_NOEXCEPT;
// Pass NULL to detach. Detach and layer replacement require stopped
// emulation; dimensions for the currently attached layer may change live.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_set_display_surface(
    const rpcs3_ios_display_surface* surface) RPCS3_IOS_NOEXCEPT;
// This setter is safe while boot/compilation owns the serialized lifecycle
// lock. player_index uses RPCS3's zero-based pad-port numbering (0...6).
// Pass connected=0 to release every input and disconnect that player.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_set_pad_state(
    uint32_t player_index,
    const rpcs3_ios_pad_state* state) RPCS3_IOS_NOEXCEPT;
// Observational and safe to poll while boot or emulation owns the serialized
// lifecycle lock. player_index uses zero-based pad-port numbering (0...6).
// Both motor values are zero when no rumble is requested.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_get_pad_feedback(
    uint32_t player_index,
    rpcs3_ios_pad_feedback* feedback) RPCS3_IOS_NOEXCEPT;
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
// Observational and safe to poll while boot or emulation owns the serialized
// lifecycle lock. Fields without a valid_fields bit must not be displayed.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_get_performance_metrics(
    rpcs3_ios_performance_metrics* metrics) RPCS3_IOS_NOEXCEPT;
// Uses the same Emulator::Pause/Resume lifecycle as the desktop frontend.
// Pause is accepted only from running; resume is accepted only from paused.
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_pause_emulation(void) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_resume_emulation(void) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_stop_emulation(void) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_state rpcs3_ios_get_state(void) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT rpcs3_ios_status rpcs3_ios_shutdown(void) RPCS3_IOS_NOEXCEPT;
RPCS3_IOS_EXPORT const char* rpcs3_ios_last_error(void) RPCS3_IOS_NOEXCEPT;

#if defined(__cplusplus)
}
#endif

#undef RPCS3_IOS_NOEXCEPT

#endif
