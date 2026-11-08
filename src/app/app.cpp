/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "app.h"

#include "application_config.h"
#include "asset_manager.h"
#include "help_popup.h"
#include "logger.h"
#include "linux_input.h"
#include "screenshot_service.h"
#include "screen_manager.h"
#include "base_viewmodel.h"
#include "theme.h"
#include "toast.h"

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

#if USE_DESKTOP
#include "desktop_simulator_frame.h"
#endif

#if !USE_DESKTOP
#if APP_USE_DRM
#include "src/drivers/display/drm/lv_linux_drm.h"
#else
#include "src/drivers/display/fb/lv_linux_fbdev.h"
#endif
#endif

#ifndef APP_FRAMEBUFFER_DEVICE
#define APP_FRAMEBUFFER_DEVICE "/dev/fb0"
#endif

#ifndef APP_DRM_DEVICE
#define APP_DRM_DEVICE "/dev/dri/card0"
#endif

#ifndef APP_DRM_CONNECTOR_ID
#define APP_DRM_CONNECTOR_ID -1
#endif

#ifndef APP_CONFIG_FILE
#define APP_CONFIG_FILE "vade.conf"
#endif

namespace app {
namespace {

void quit_requested_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* running = static_cast<bool*>(lv_observer_get_user_data(observer));
    if (running && lv_subject_get_int(subject)) {
        *running = false;
    }
}

struct DarkModePersistence {
    std::string config_path;
    bool last_dark_mode;
};

std::string writable_config_path() {
#if USE_DESKTOP
    return APP_CONFIG_FILE;
#else
    if (const char* xdg_config_home = std::getenv("XDG_CONFIG_HOME")) {
        const std::filesystem::path root(xdg_config_home);
        if (!root.empty() && root.is_absolute()) {
            return (root / "vade" / "vade.conf").string();
        }
    }
    if (const char* home = std::getenv("HOME")) {
        const std::filesystem::path root(home);
        if (!root.empty() && root.is_absolute()) {
            return (root / ".config" / "vade" / "vade.conf").string();
        }
    }
    return APP_CONFIG_FILE;
#endif
}

void persist_dark_mode_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* persistence = static_cast<DarkModePersistence*>(lv_observer_get_user_data(observer));
    if (!persistence) {
        return;
    }
    const bool dark_mode = lv_subject_get_int(subject) != 0;
    if (dark_mode == persistence->last_dark_mode) {
        return;
    }
    persistence->last_dark_mode = dark_mode;

    ApplicationConfig config;
    config.dark_mode = dark_mode;
    std::string error;
    if (save_application_config(persistence->config_path, config, error)) {
        LOG_INFO("saved config: {} (dark_mode={})",
                 persistence->config_path,
                 config.dark_mode ? "yes" : "no");
    } else {
        LOG_WARN("failed to save config {}: {}", persistence->config_path, error);
    }
}

class ShortcutController {
public:
    ShortcutController(viewmodel::BaseViewModel& view_model, AssetManager& assets)
        : view_model_(view_model),
          toast_(std::make_unique<view::widgets::Toast>(lv_layer_top(), view_model, assets)),
          help_popup_(std::make_unique<view::widgets::HelpPopup>(lv_layer_top(), view_model, assets)) {
        toast_->build();
        help_popup_->build();
        platform::set_global_key_listener(global_key_listener, this);
        platform::set_key_release_listener(key_release_listener, this);
    }

    ~ShortcutController() {
        platform::clear_key_release_listener(key_release_listener, this);
        platform::clear_global_key_listener(global_key_listener, this);
    }

private:
    bool handle_key(uint32_t key, bool long_pressed) {
        if (key == platform::kKeyPrintScreen) {
            if (!long_pressed) {
                const auto result = platform::screenshot::capture_active_screen();
                if (result.success) {
                    LOG_INFO("screenshot saved: {}", result.path);
                    toast_->show_highlighted("Saved to ",
                                             result.path,
                                             "",
                                             view::widgets::ToastTone::Success,
                                             5200,
                                             true);
                }
                else {
                    LOG_WARN("screenshot failed: {}", result.error);
                    toast_->show("Screenshot failed: " + result.error,
                                 view::widgets::ToastTone::Error,
                                 3200);
                }
            }
            return true;
        }

        if (key == platform::kKeyHelp) {
            if (!long_pressed) {
                toast_->hide();
                if (help_popup_->visible()) {
                    help_popup_->hide();
                }
                else {
                    help_popup_->show(view_model_.current_page());
                }
            }
            return true;
        }

        if (help_popup_->visible()) {
            if (key == LV_KEY_ESC && !long_pressed) {
                help_popup_->hide();
            }
            return true;
        }

        const bool is_escape = key == LV_KEY_ESC;

        if (is_escape) {
            if (long_pressed) {
                // 官方语义：长按 ESC 直接退出应用（hold 即确认）
                toast_->hide();
                view_model_.request_quit();
                return true;
            }
            exit_key_pressed_ = key;
            if (view_model_.ime_composing_active()) {
                view_model_.ime_cancel();  // Esc：先取消组词
            }
            return true;
        }

        if (long_pressed) {
            return false;
        }

        // 检索页：拼音组词 / 字符输入 / 列表导航（4~8 留给底部导航，不进输入）
        if (view_model_.current_page() == model::AppPage::Search) {
            const bool composing = view_model_.ime_composing_active();
            if (key == LV_KEY_UP) {
                if (composing) view_model_.ime_candidate_prev();
                else view_model_.select_prev_result();
                return true;
            }
            if (key == LV_KEY_DOWN) {
                if (composing) view_model_.ime_candidate_next();
                else view_model_.select_next_result();
                return true;
            }
            if (key == LV_KEY_ENTER) {
                if (composing) view_model_.ime_commit_selected();
                else view_model_.open_selected_result();
                return true;
            }
            if (key == ' ') {
                if (composing) view_model_.ime_commit_first();
                else view_model_.append_query_char(' ');
                return true;
            }
            if (key == '\t') {
                view_model_.toggle_ime_mode();  // Tab：中/英切换
                return true;
            }
            if (key == LV_KEY_BACKSPACE || key == 0x08 || key == 0x7F) {
                if (composing) view_model_.ime_backspace();
                else view_model_.backspace_query();
                return true;
            }
            if (key >= 32 && key < 127 && key != '4' && key != '5' && key != '6' &&
                key != '7' && key != '8') {
                const char c = static_cast<char>(key);
                const bool is_alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
                if (view_model_.ime_chinese_mode() && is_alpha) {
                    view_model_.ime_append_letter(c);
                } else {
                    view_model_.append_query_char(c);
                }
                return true;
            }
        }
        return false;
    }

    void handle_release(uint32_t key) {
        if (key != exit_key_pressed_) {
            return;
        }

        if (exit_armed_) {
            toast_->hide();
        } else if (key == LV_KEY_ESC) {
            const auto page = view_model_.current_page();
            if (page == model::AppPage::Reader) {
                view_model_.leave_reader();
            } else if (page == model::AppPage::Search) {
                view_model_.set_query_text("");
            } else {
                view_model_.show_search_page();
            }
        }
        exit_key_pressed_ = 0;
        exit_armed_ = false;
    }

    static bool global_key_listener(uint32_t key,
                                    const char*,
                                    bool long_pressed,
                                    void* user_data) {
        auto* controller = static_cast<ShortcutController*>(user_data);
        return controller && controller->handle_key(key, long_pressed);
    }

    static void key_release_listener(uint32_t key, const char*, void* user_data) {
        auto* controller = static_cast<ShortcutController*>(user_data);
        if (controller) {
            controller->handle_release(key);
        }
    }

    viewmodel::BaseViewModel& view_model_;
    std::unique_ptr<view::widgets::Toast> toast_;
    std::unique_ptr<view::widgets::HelpPopup> help_popup_;
    uint32_t exit_key_pressed_{0};
    bool exit_armed_{false};
};

#if !USE_DESKTOP
lv_display_t* init_device_display() {
#if APP_USE_DRM
    auto* display = lv_linux_drm_create();
    if (!display) {
        return nullptr;
    }

    if (lv_linux_drm_set_file(display, APP_DRM_DEVICE, APP_DRM_CONNECTOR_ID) != LV_RESULT_OK) {
        lv_display_delete(display);
        return nullptr;
    }

    platform::init_key_input(display);
    return display;
#else
    auto* display = lv_linux_fbdev_create();
    if (!display) {
        return nullptr;
    }

    if (lv_linux_fbdev_set_file(display, APP_FRAMEBUFFER_DEVICE) != LV_RESULT_OK) {
        lv_display_delete(display);
        return nullptr;
    }

    platform::init_key_input(display);
    return display;
#endif
}
#endif

} // namespace

int Application::run() {
    logger::Logger::init();
    logger::Logger::set_tag("vade");

    lv_init();

    AssetManager assets;
    for (const auto& root : assets.roots()) {
        LOG_INFO("asset root: {}", root.string());
    }

    viewmodel::BaseViewModel view_model;
    const std::string user_config_path = writable_config_path();
    std::string loaded_config_path = APP_CONFIG_FILE;
    if (user_config_path != APP_CONFIG_FILE) {
        std::error_code filesystem_error;
        if (std::filesystem::is_regular_file(user_config_path, filesystem_error)) {
            loaded_config_path = user_config_path;
        }
    }

    ApplicationConfig config;
    std::string config_error;
    if (load_application_config(loaded_config_path, config, config_error)) {
        view_model.set_dark_mode(config.dark_mode);
        LOG_INFO("loaded config: {} (dark_mode={})",
                 loaded_config_path,
                 config.dark_mode ? "yes" : "no");
    } else {
        LOG_WARN("failed to load config {}: {}; using defaults", loaded_config_path, config_error);
    }

#if USE_DESKTOP
    DesktopSimulatorFrame simulator_frame(assets);
    auto* display = simulator_frame.display();
#else
    auto* display = init_device_display();
#endif
    if (!display) {
        LOG_ERROR("failed to initialize display");
        return 1;
    }

    auto* standard_font = assets.load_standard_font(14);
    view::apply_lvgl_theme(display,
                           view_model.is_dark_mode(),
                           standard_font ? standard_font : LV_FONT_DEFAULT);

#if USE_DESKTOP
    simulator_frame.bind_dark_mode(view_model.dark_mode_subject());
#endif

    // 词库：优先用户目录（免重装更新），回落随包资源
    if (!view_model.load_ime_dict("/home/mars/.local/share/vade/pinyin_dict.txt")) {
        view_model.load_ime_dict(assets.resolve("data/pinyin_dict.txt").string());
    }

    for (const char* index_path : {
             static_cast<const char*>(std::getenv("VADE_INDEX_DB")),  // 桌面/测试可用环境变量指定索引库
             "/home/mars/.local/share/vade/vade_index.db",
             "/usr/share/vade/data/vade_index.db",
         }) {
        if (index_path && view_model.load_search_db(index_path)) {
            LOG_INFO("FTS index loaded: {}", index_path);
            break;
        }
    }

    ScreenManager screen_manager(view_model, assets);
    screen_manager.start();
    ShortcutController shortcuts(view_model, assets);

    // ── 自测键注入器：VADE_TEST_KEYS="z,h,i,enter"（逗号分词，longesc=长按ESC）
    // 直接打进键路由，模拟物理按键全链路，供模拟器自动化回归使用
    struct TestKeyDriver {
        std::vector<std::string> tokens;
        std::size_t index = 0;
        int tick_in_token = 0;
        lv_timer_t* timer = nullptr;

        static uint32_t token_key(const std::string& t) {
            if (t == "enter") return LV_KEY_ENTER;
            if (t == "up") return LV_KEY_UP;
            if (t == "down") return LV_KEY_DOWN;
            if (t == "left") return LV_KEY_LEFT;
            if (t == "right") return LV_KEY_RIGHT;
            if (t == "tab") return '\t';
            if (t == "backspace") return LV_KEY_BACKSPACE;
            if (t == "esc" || t == "longesc") return LV_KEY_ESC;
            return t.empty() ? 0 : static_cast<uint32_t>(t[0]);
        }

        static void tick(lv_timer_t* timer) {
            auto* self = static_cast<TestKeyDriver*>(lv_timer_get_user_data(timer));
            if (!self) return;
            if (self->index >= self->tokens.size()) {
                lv_timer_pause(timer);
                return;
            }
            const std::string& tok = self->tokens[self->index];
            const uint32_t key = token_key(tok);
            const int hold_ticks = (tok == "longesc") ? 4 : 1;
            if (self->tick_in_token == 0) {
                platform::inject_key(key, true);
            }
            self->tick_in_token++;
            if (self->tick_in_token > hold_ticks) {
                platform::inject_key(key, false);
                self->tick_in_token = 0;
                self->index++;
            }
        }
    };
    TestKeyDriver test_keys;
    if (const char* env_keys = std::getenv("VADE_TEST_KEYS")) {
        std::string list(env_keys);
        std::size_t pos = 0;
        while (pos <= list.size()) {
            const auto comma = list.find(',', pos);
            const auto tok = list.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            if (!tok.empty()) test_keys.tokens.push_back(tok);
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        test_keys.timer = lv_timer_create(TestKeyDriver::tick, 200, &test_keys);
        LOG_INFO("test key driver armed: {} tokens", test_keys.tokens.size());
    }

    DarkModePersistence dark_mode_persistence{user_config_path, view_model.is_dark_mode()};
    auto* dark_mode_observer = lv_subject_add_observer(view_model.dark_mode_subject(),
                                                        persist_dark_mode_observer,
                                                        &dark_mode_persistence);

    bool running = true;
    auto* quit_observer = lv_subject_add_observer(view_model.quit_requested_subject(),
                                                  quit_requested_observer,
                                                  &running);

    LOG_INFO("LVGL app started at {}x{}", lv_display_get_horizontal_resolution(display),
             lv_display_get_vertical_resolution(display));
    while (running
#if USE_DESKTOP
           && simulator_frame.process_events()
#endif
    ) {
        lv_timer_handler();
        // 注意：LVGL 默认 lv_delay_ms 是忙等（烧 CPU），必须用真睡眠
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (quit_observer) {
        lv_observer_remove(quit_observer);
    }
    if (dark_mode_observer) {
        lv_observer_remove(dark_mode_observer);
    }

    // 退出前清屏：防止应用残帧留在 fb 上与启动器叠印（2026-01-09 实机教训）
    lv_obj_t* black = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(black);
    lv_obj_set_size(black, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(black, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(black, LV_OPA_COVER, 0);
    lv_obj_move_foreground(black);
    lv_refr_now(nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    return 0;
}

} // namespace app
