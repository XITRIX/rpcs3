//
//  RPCS3.mm
//  RPCS3
//
//  Created by Даниил Виноградов on 21.02.2025.
//

#include <Foundation/Foundation.h>
#include "RPCS3.hpp"

#include "Emu/System.h"
#include "Emu/RSX/Null/NullGSRender.h"
#include "Emu/Cell/Modules/cellMsgDialog.h"
#include "Emu/Cell/Modules/cellOskDialog.h"
#include "Emu/Cell/Modules/cellSaveData.h"
#include "Emu/Cell/Modules/sceNpTrophy.h"
#include "Emu/Io/Null/null_camera_handler.h"
#include "Emu/Io/Null/null_music_handler.h"
#include "Emu/system_utils.hpp"
#include "Utilities/File.h"

RPCS3* RPCS3::_shared = new RPCS3();

LOG_CHANNEL(sys_log, "SYS");
LOG_CHANNEL(q_debug, "QDEBUG");


void RPCS3::callFromMainThread(std::function<void()> func) {
    Emu.CallFromMainThread([func]()
    {
        func();
    });
}

struct log_listener final : logs::listener {
    void log(u64 /*stamp*/, const logs::message& msg, const std::string& prefix, const std::string& text) override
    {
        printf("[%s]: %s\n", prefix.c_str(), text.c_str());
    }
};

RPCS3::RPCS3() {
    static std::unique_ptr<log_listener> logs_listener = std::make_unique<log_listener>();
    logs::listener::add(logs_listener.get());

    {
        // Write RPCS3 version
        logs::stored_message ver{sys_log.always()};
//        ver.text = fmt::format("RPCS3 v%s", rpcs3::get_verbose_version());

        // Write System information
//        logs::stored_message sys{sys_log.always()};
//        sys.text = utils::get_system_info();

        // Write OS version
//        logs::stored_message os{sys_log.always()};
//        os.text = utils::get_OS_version_string();

        // Write Qt version
//        logs::stored_message qt{(strcmp(QT_VERSION_STR, qVersion()) != 0) ? sys_log.error : sys_log.notice};
//        qt.text = fmt::format("Qt version: Compiled against Qt %s | Run-time uses Qt %s", QT_VERSION_STR, qVersion());

        // Write current time
        logs::stored_message time{sys_log.always()};
        time.text = fmt::format("Current Time: %s", std::chrono::system_clock::now());

        logs::set_init({std::move(ver), std::move(time)});
    }

    Emu.SetHasGui(false);
    Emu.SetUsr("00000001");
//    Emu.Init();

    // Create callbacks from the emulator, which reference the handlers.
    InitializeCallbacks();

    Emu.CallFromMainThread([]()
    {

    });

//    InitializeEmulator(m_active_user.empty() ? "00000001" : m_active_user, false);
}

void RPCS3::InitializeCallbacks()
{
    EmuCallbacks callbacks = CreateCallbacks();

    callbacks.try_to_quit = [this](bool force_quit, std::function<void()> on_exit) -> bool
    {
        if (force_quit)
        {
            if (on_exit)
            {
                on_exit();
            }

            exit(0);
            return true;
        }

        return false;
    };
    callbacks.call_from_main_thread = [this](std::function<void()> func, atomic_t<u32>* wake_up)
    {
        dispatch_async(dispatch_get_main_queue(), ^{
            func();

            if (wake_up)
            {
                *wake_up = true;
                wake_up->notify_one();
            }
        });
    };

    callbacks.init_gs_render = [](utils::serial* ar)
    {
        switch (const video_renderer type = g_cfg.video.renderer)
        {
        case video_renderer::null:
        {
            g_fxo->init<rsx::thread, named_thread<NullGSRender>>(ar);
            break;
        }
        case video_renderer::opengl:
        case video_renderer::vulkan:
        {
            fmt::throw_exception("Headless mode can only be used with the %s video renderer. Current renderer: %s", video_renderer::null, type);
            [[fallthrough]];
        }
        default:
        {
            fmt::throw_exception("Invalid video renderer: %s", type);
        }
        }
    };

    callbacks.get_camera_handler = []() -> std::shared_ptr<camera_handler_base>
    {
        switch (g_cfg.io.camera.get())
        {
        case camera_handler::null:
        case camera_handler::fake:
        {
            return std::make_shared<null_camera_handler>();
        }
        case camera_handler::qt:
        {
            fmt::throw_exception("Headless mode can not be used with this camera handler. Current handler: %s", g_cfg.io.camera.get());
        }
        }
        return nullptr;
    };

    callbacks.get_music_handler = []() -> std::shared_ptr<music_handler_base>
    {
        switch (g_cfg.audio.music.get())
        {
        case music_handler::null:
        {
            return std::make_shared<null_music_handler>();
        }
        case music_handler::qt:
        {
            fmt::throw_exception("Headless mode can not be used with this music handler. Current handler: %s", g_cfg.audio.music.get());
        }
        }
        return nullptr;
    };

    callbacks.close_gs_frame = [](){};
    callbacks.get_gs_frame = []() -> std::unique_ptr<GSFrameBase>
    {
        if (g_cfg.video.renderer != video_renderer::null)
        {
            fmt::throw_exception("Headless mode can only be used with the %s video renderer. Current renderer: %s", video_renderer::null, g_cfg.video.renderer.get());
        }
        return std::unique_ptr<GSFrameBase>();
    };

    callbacks.get_msg_dialog                 = []() -> std::shared_ptr<MsgDialogBase> { return std::shared_ptr<MsgDialogBase>(); };
    callbacks.get_osk_dialog                 = []() -> std::shared_ptr<OskDialogBase> { return std::shared_ptr<OskDialogBase>(); };
    callbacks.get_save_dialog                = []() -> std::unique_ptr<SaveDialogBase> { return std::unique_ptr<SaveDialogBase>(); };
    callbacks.get_trophy_notification_dialog = []() -> std::unique_ptr<TrophyNotificationBase> { return std::unique_ptr<TrophyNotificationBase>(); };

    callbacks.on_run    = [](bool /*start_playtime*/) {};
    callbacks.on_pause  = []() {};
    callbacks.on_resume = []() {};
    callbacks.on_stop   = []() {};
    callbacks.on_ready  = []() {};
    callbacks.on_emulation_stop_no_response = [](std::shared_ptr<atomic_t<bool>> closed_successfully, int /*seconds_waiting_already*/)
    {
        if (!closed_successfully || !*closed_successfully)
        {
//            report_fatal_error(tr("Stopping emulator took too long."
//                        "\nSome thread has probably deadlocked. Aborting.").toStdString());
        }
    };

    callbacks.on_save_state_progress = [](std::shared_ptr<atomic_t<bool>>, stx::shared_ptr<utils::serial>, stx::atomic_ptr<std::string>*, std::shared_ptr<void>)
    {
    };

    callbacks.enable_disc_eject  = [](bool) {};
    callbacks.enable_disc_insert = [](bool) {};

    callbacks.on_missing_fw = []() {};

    callbacks.handle_taskbar_progress = [](s32, s32) {};

    callbacks.get_localized_string    = [](localized_string_id, const char*) -> std::string { return {}; };
    callbacks.get_localized_u32string = [](localized_string_id, const char*) -> std::u32string { return {}; };
    callbacks.get_localized_setting   = [](const cfg::_base*, u32) -> std::string { return {}; };

    callbacks.play_sound = [](const std::string&){};
    callbacks.add_breakpoint = [](u32 /*addr*/){};

    Emu.SetCallbacks(std::move(callbacks));
}

void RPCS3::OnEmuSettingsChange()
{
    if (Emu.IsRunning())
    {
        if (g_cfg.misc.prevent_display_sleep)
        {
//            disable_display_sleep();
        }
        else
        {
//            enable_display_sleep();
        }
    }

    if (!Emu.IsStopped())
    {
        // Change logging (only allowed during gameplay)
        rpcs3::utils::configure_logs();

        // Force audio provider
        g_cfg.audio.provider.set(Emu.IsVsh() ? audio_provider::rsxaudio : audio_provider::cell_audio);
    }

//    audio::configure_audio();
//    audio::configure_rsxaudio();
//    rsx::overlays::reset_performance_overlay();
//    rsx::overlays::reset_debug_overlay();
}

EmuCallbacks RPCS3::CreateCallbacks()
{
    EmuCallbacks callbacks{};

    callbacks.update_emu_settings = [this]()
    {
        Emu.CallFromMainThread([&]()
        {
            OnEmuSettingsChange();
        });
    };

    callbacks.save_emu_settings = [this]()
    {
        Emu.BlockingCallFromMainThread([&]()
        {
            Emulator::SaveSettings(g_cfg.to_string(), Emu.GetTitleID());
        });
    };

    callbacks.init_kb_handler = [this]()
    {
        switch (g_cfg.io.keyboard.get())
        {
        case keyboard_handler::null:
        {
//            ensure(g_fxo->init<KeyboardHandlerBase, NullKeyboardHandler>(Emu.DeserialManager()));
            break;
        }
        case keyboard_handler::basic:
        {
//            basic_keyboard_handler* ret = g_fxo->init<KeyboardHandlerBase, basic_keyboard_handler>(Emu.DeserialManager());
//            ensure(ret);
//            ret->moveToThread(get_thread());
//            ret->SetTargetWindow(reinterpret_cast<QWindow*>(m_game_window));
            break;
        }
        }
    };

    callbacks.init_mouse_handler = [this]()
    {
        mouse_handler handler = g_cfg.io.mouse;

        if (handler == mouse_handler::null)
        {
            switch (g_cfg.io.move)
            {
            case move_handler::mouse:
                handler = mouse_handler::basic;
                break;
            case move_handler::raw_mouse:
                handler = mouse_handler::raw;
                break;
            default:
                break;
            }
        }

        switch (handler)
        {
        case mouse_handler::null:
        {
//            ensure(g_fxo->init<MouseHandlerBase, NullMouseHandler>(Emu.DeserialManager()));
            break;
        }
        case mouse_handler::basic:
        {
//            basic_mouse_handler* ret = g_fxo->init<MouseHandlerBase, basic_mouse_handler>(Emu.DeserialManager());
//            ensure(ret);
//            ret->moveToThread(get_thread());
//            ret->SetTargetWindow(reinterpret_cast<QWindow*>(m_game_window));
            break;
        }
        case mouse_handler::raw:
        {
//            ensure(g_fxo->init<MouseHandlerBase, raw_mouse_handler>(Emu.DeserialManager()));
            break;
        }
        }
    };

    callbacks.init_pad_handler = [this](std::string_view title_id)
    {
//        ensure(g_fxo->init<named_thread<pad_thread>>(get_thread(), m_game_window, title_id));
//
//        qt_events_aware_op(0, [](){ return !!pad::g_started; });
    };

    callbacks.get_audio = []() -> std::shared_ptr<AudioBackend>
    {
        std::shared_ptr<AudioBackend> result;
//        switch (g_cfg.audio.renderer.get())
//        {
//        case audio_renderer::null: result = std::make_shared<NullAudioBackend>(); break;
//#ifdef _WIN32
//        case audio_renderer::xaudio: result = std::make_shared<XAudio2Backend>(); break;
//#endif
//        case audio_renderer::cubeb: result = std::make_shared<CubebBackend>(); break;
//#ifdef HAVE_FAUDIO
//        case audio_renderer::faudio: result = std::make_shared<FAudioBackend>(); break;
//#endif
//        }
//
//        if (!result->Initialized())
//        {
//            // Fall back to a null backend if something went wrong
//            sys_log.error("Audio renderer %s could not be initialized, using a Null renderer instead. Make sure that no other application is running that might block audio access (e.g. Netflix).", result->GetName());
//            result = std::make_shared<NullAudioBackend>();
//        }
        return result;
    };

    callbacks.get_audio_enumerator = [](u64 renderer) -> std::shared_ptr<audio_device_enumerator>
    {
        switch (static_cast<audio_renderer>(renderer))
        {
//        case audio_renderer::null: return std::make_shared<null_enumerator>();
#ifdef _WIN32
        case audio_renderer::xaudio: return std::make_shared<xaudio2_enumerator>();
#endif
//        case audio_renderer::cubeb: return std::make_shared<cubeb_enumerator>();
#ifdef HAVE_FAUDIO
        case audio_renderer::faudio: return std::make_shared<faudio_enumerator>();
#endif
        default: fmt::throw_exception("Invalid renderer index %u", renderer);
        }
    };

    callbacks.get_image_info = [](const std::string& filename, std::string& sub_type, s32& width, s32& height, s32& orientation) -> bool
    {
        sub_type.clear();
        width = 0;
        height = 0;
        orientation = 0; // CELL_SEARCH_ORIENTATION_UNKNOWN

        bool success = false;
        Emu.BlockingCallFromMainThread([&]()
        {
//            const QImageReader reader(QString::fromStdString(filename));
//            if (reader.canRead())
//            {
//                const QSize size = reader.size();
//                width = size.width();
//                height = size.height();
//                sub_type = reader.subType().toStdString();
//
//                switch (reader.transformation())
//                {
//                case QImageIOHandler::Transformation::TransformationNone:
//                    orientation = 1; // CELL_SEARCH_ORIENTATION_TOP_LEFT = 0°
//                    break;
//                case QImageIOHandler::Transformation::TransformationRotate90:
//                    orientation = 2; // CELL_SEARCH_ORIENTATION_TOP_RIGHT = 90°
//                    break;
//                case QImageIOHandler::Transformation::TransformationRotate180:
//                    orientation = 3; // CELL_SEARCH_ORIENTATION_BOTTOM_RIGHT = 180°
//                    break;
//                case QImageIOHandler::Transformation::TransformationRotate270:
//                    orientation = 4; // CELL_SEARCH_ORIENTATION_BOTTOM_LEFT = 270°
//                    break;
//                default:
//                    // Ignore other transformations for now
//                    break;
//                }
//
//                success = true;
//                sys_log.notice("get_image_info found image: filename='%s', sub_type='%s', width=%d, height=%d, orientation=%d", filename, sub_type, width, height, orientation);
//            }
//            else
//            {
//                sys_log.error("get_image_info failed to read '%s'. Error='%s'", filename, reader.errorString());
//            }
        });
        return success;
    };

    callbacks.get_scaled_image = [](const std::string& path, s32 target_width, s32 target_height, s32& width, s32& height, u8* dst, bool force_fit) -> bool
    {
        width = 0;
        height = 0;

        if (target_width <= 0 || target_height <= 0 || !dst || !fs::is_file(path))
        {
            return false;
        }

        bool success = false;
        Emu.BlockingCallFromMainThread([&]()
        {
//            // We use QImageReader instead of QImage. This way we can load and scale image in one step.
//            QImageReader reader(QString::fromStdString(path));
//
//            if (reader.canRead())
//            {
//                QSize size = reader.size();
//                width = size.width();
//                height = size.height();
//
//                if (width <= 0 || height <= 0)
//                {
//                    return;
//                }
//
//                if (force_fit || width > target_width || height > target_height)
//                {
//                    const f32 target_ratio = target_width / static_cast<f32>(target_height);
//                    const f32 image_ratio = width / static_cast<f32>(height);
//                    const f32 convert_ratio = image_ratio / target_ratio;
//
//                    if (convert_ratio > 1.0f)
//                    {
//                        size = QSize(target_width, target_height / convert_ratio);
//                    }
//                    else if (convert_ratio < 1.0f)
//                    {
//                        size = QSize(target_width * convert_ratio, target_height);
//                    }
//                    else
//                    {
//                        size = QSize(target_width, target_height);
//                    }
//
//                    reader.setScaledSize(size);
//                    width = size.width();
//                    height = size.height();
//                }
//
//                QImage image = reader.read();
//
//                if (image.format() != QImage::Format::Format_RGBA8888)
//                {
//                    image = image.convertToFormat(QImage::Format::Format_RGBA8888);
//                }
//
//                std::memcpy(dst, image.constBits(), std::min(target_width * target_height * 4LL, image.height() * image.bytesPerLine()));
//                success = true;
//                sys_log.notice("get_scaled_image scaled image: path='%s', width=%d, height=%d", path, width, height);
//            }
//            else
//            {
//                sys_log.error("get_scaled_image failed to read '%s'. Error='%s'", path, reader.errorString());
//            }
        });
        return success;
    };

//    callbacks.resolve_path = [](std::string_view sv)
//    {
//        // May result in an empty string if path does not exist
//        return QFileInfo(QString::fromUtf8(sv.data(), static_cast<int>(sv.size()))).canonicalFilePath().toStdString();
//    };

//    callbacks.get_font_dirs = []()
//    {
//        const QStringList locations = QStandardPaths::standardLocations(QStandardPaths::FontsLocation);
//        std::vector<std::string> font_dirs;
//        for (const QString& location : locations)
//        {
//            std::string font_dir = location.toStdString();
//            if (!font_dir.ends_with('/'))
//            {
//                font_dir += '/';
//            }
//            font_dirs.push_back(font_dir);
//        }
//        return font_dirs;
//    };

    callbacks.on_install_pkgs = [](const std::vector<std::string>& pkgs)
    {
        for (const std::string& pkg : pkgs)
        {
            if (!rpcs3::utils::install_pkg(pkg))
            {
                sys_log.error("Failed to install %s", pkg);
                return false;
            }
        }
        return true;
    };

    return callbacks;
}
