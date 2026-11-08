/*
 * Vade — pocket companion terminal
 * SPDX-License-Identifier: MIT
 */
#include "navbar.h"

#include "linux_input.h"
#include "bindings.h"

#include <cstdint>

namespace view::widgets {

namespace {
// 官方底部功能图标坐标：中心 x=49/105/161/217/273，y=153（屏幕坐标），导航条 30px（y=140..170）
constexpr int32_t kCentersX[5] = {49, 105, 161, 217, 273};
constexpr const char* kIcons[5] = {"\u68C0\u7D22", "\u5BF9\u8BDD", "\u4EFB\u52A1", "\u91C7\u96C6", "\u8BBE\u7F6E"};  // 检索 对话 任务 采集 设置
constexpr const char* kKeys[5] = {"4", "5", "6", "7", "8"};
constexpr int32_t kButtonW = 56;
constexpr int32_t kBarH = 30;
} // namespace

NavBar::NavBar(lv_obj_t* parent, viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseWidgets(parent), view_model_(view_model), assets_(assets) {}

NavBar::~NavBar() {
    teardown();
}

void NavBar::teardown() {
    for (int i = 0; i < 5; i++) {
        if (buttons_[i] && lv_obj_is_valid(buttons_[i])) {
            platform::unregister_nav_button(i, buttons_[i]);
        }
        buttons_[i] = nullptr;
        icon_labels_[i] = nullptr;
    }
    if (page_observer_) {
        lv_observer_remove(page_observer_);
        page_observer_ = nullptr;
    }
    if (theme_observer_) {
        lv_observer_remove(theme_observer_);
        theme_observer_ = nullptr;
    }
}

void NavBar::build() {
    core_obj_ = lv_obj_create(parent_);
    lv_obj_remove_style_all(core_obj_);
    lv_obj_set_size(core_obj_, 320, kBarH);
    lv_obj_align(core_obj_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_SCROLLABLE);
    reactive::bind_theme(core_obj_, view_model_.dark_mode_subject(), reactive::ThemeRole::Bar);

    auto* icon_font = assets_.load_standard_font(12, app::StandardFontWeight::Bold);
    auto* key_font = assets_.load_standard_font(12, app::StandardFontWeight::Regular);

    for (int i = 0; i < 5; i++) {
        auto* btn = lv_obj_create(core_obj_);
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, kButtonW, kBarH);
        lv_obj_set_pos(btn, kCentersX[i] - kButtonW / 2, 0);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(btn, button_clicked_cb, LV_EVENT_CLICKED, this);
        buttons_[i] = btn;

        auto* icon = lv_label_create(btn);
        lv_label_set_text(icon, kIcons[i]);
        if (icon_font) {
            lv_obj_set_style_text_font(icon, icon_font, 0);
        }
        lv_obj_align(icon, LV_ALIGN_CENTER, 0, -6);
        reactive::bind_theme(icon, view_model_.dark_mode_subject(), reactive::ThemeRole::Text);
        icon_labels_[i] = icon;

        auto* key = lv_label_create(btn);
        lv_label_set_text(key, kKeys[i]);
        if (key_font) {
            lv_obj_set_style_text_font(key, key_font, 0);
        }
        lv_obj_align(key, LV_ALIGN_CENTER, 0, 8);
        reactive::bind_theme(key, view_model_.dark_mode_subject(), reactive::ThemeRole::Text);
        lv_obj_set_style_text_opa(key, LV_OPA_50, 0);

        platform::register_nav_button(i, btn);
    }

    page_observer_ = reactive::observe_obj(core_obj_, view_model_.current_page_subject(), update_icons_cb, this);
    theme_observer_ = reactive::observe_obj(core_obj_, view_model_.dark_mode_subject(), update_icons_cb, this);
    refresh_icons();
}

void NavBar::button_clicked_cb(lv_event_t* event) {
    auto* self = static_cast<NavBar*>(lv_event_get_user_data(event));
    auto* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    if (!self || !btn) {
        return;
    }
    for (size_t i = 0; i < 5; i++) {
        if (self->buttons_[i] == btn) {
            self->on_button(i);
            return;
        }
    }
}

void NavBar::on_button(size_t index) {
    switch (index) {
        case 0:
            if (view_model_.current_page() == model::AppPage::Search && !view_model_.query_text().empty()) {
                view_model_.set_query_text("");
            } else {
                view_model_.show_search_page();
            }
            break;
        case 1: view_model_.show_chat_page(); break;
        case 2: view_model_.show_tasks_page(); break;
        case 3: view_model_.show_capture_page(); break;
        case 4: view_model_.show_settings_page(); break;
        default: break;
    }
}

void NavBar::update_icons_cb(lv_observer_t* observer, lv_subject_t* subject) {
    LV_UNUSED(subject);
    auto* self = static_cast<NavBar*>(lv_observer_get_user_data(observer));
    if (self) {
        self->refresh_icons();
    }
}

void NavBar::refresh_icons() {
    const auto page = view_model_.current_page();
    for (int i = 0; i < 5; i++) {
        if (!icon_labels_[i] || !lv_obj_is_valid(icon_labels_[i])) {
            continue;
        }
        const bool active = static_cast<int>(page) == i ||
                            (page == model::AppPage::Reader && i == 0);
        lv_obj_set_style_text_opa(icon_labels_[i], active ? LV_OPA_COVER : LV_OPA_60, 0);
    }
}

} // namespace view::widgets
