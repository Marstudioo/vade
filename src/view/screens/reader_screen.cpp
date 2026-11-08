/*
 * Vade — pocket companion terminal
 * SPDX-License-Identifier: MIT
 */
#include "reader_screen.h"

#include "bindings.h"

namespace screen {

void ReaderScreen::build_content(lv_obj_t* content) {
    title_label_ = lv_label_create(content);
    lv_label_set_text(title_label_, "");
    auto* title_font = assets().load_standard_font(15, app::StandardFontWeight::Bold);
    if (title_font) {
        lv_obj_set_style_text_font(title_label_, title_font, 0);
    }
    lv_label_set_long_mode(title_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title_label_, 304);
    lv_obj_align(title_label_, LV_ALIGN_TOP_MID, 0, 0);
    reactive::bind_theme(title_label_, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    // 正文滚动容器（键盘 ↑↓ 滚动：焦点在此对象上）
    scroll_container_ = lv_obj_create(content);
    lv_obj_remove_style_all(scroll_container_);
    lv_obj_set_size(scroll_container_, 304, 102);
    lv_obj_align(scroll_container_, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_set_scrollbar_mode(scroll_container_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(scroll_container_, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    body_label_ = lv_label_create(scroll_container_);
    lv_label_set_text(body_label_, "");
    auto* body_font = assets().load_standard_font(15, app::StandardFontWeight::Regular);
    if (body_font) {
        lv_obj_set_style_text_font(body_label_, body_font, 0);
    }
    lv_label_set_long_mode(body_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body_label_, 292);
    lv_obj_align(body_label_, LV_ALIGN_TOP_LEFT, 0, 0);
    reactive::bind_theme(body_label_, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* footer = lv_label_create(content);
    lv_label_set_text(footer, "\u2191\u2193 \u6EDA\u52A8  \u00B7  Esc \u8FD4\u56DE  \u00B7  fn+H \u5E2E\u52A9");  // ↑↓ 滚动 · Esc 返回 · fn+H 帮助
    auto* hint_font = assets().load_standard_font(12, app::StandardFontWeight::Regular);
    if (hint_font) {
        lv_obj_set_style_text_font(footer, hint_font, 0);
    }
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_opa(footer, LV_OPA_50, 0);
    reactive::bind_theme(footer, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    reader_observer_ = reactive::observe_obj(body_label_, view_model().reader_version_subject(),
                                             reader_changed_cb, this);
    font_observer_ = reactive::observe_obj(body_label_, view_model().font_level_subject(),
                                           reader_changed_cb, this);
    refresh_content();
}

void ReaderScreen::reader_changed_cb(lv_observer_t* observer, lv_subject_t* subject) {
    LV_UNUSED(subject);
    auto* self = static_cast<ReaderScreen*>(lv_observer_get_user_data(observer));
    if (self) {
        self->refresh_content();
    }
}

void ReaderScreen::refresh_content() {
    if (!body_label_ || !lv_obj_is_valid(body_label_)) {
        return;
    }
    lv_label_set_text(title_label_, view_model().reader_title().c_str());
    lv_label_set_text(body_label_, view_model().reader_body().c_str());

    static const int kBodySizes[3] = {13, 15, 18};
    auto* body_font = assets().load_standard_font(kBodySizes[view_model().font_level()],
                                                  app::StandardFontWeight::Regular);
    if (body_font) {
        lv_obj_set_style_text_font(body_label_, body_font, 0);
    }
    lv_obj_scroll_to_y(scroll_container_, 0, LV_ANIM_OFF);

    // 键盘 ↑↓ 滚动：把焦点交给滚动容器
    lv_group_t* group = lv_group_get_default();
    if (!group) {
        group = lv_group_create();
        lv_group_set_default(group);
    }
    lv_group_add_obj(group, scroll_container_);
    lv_group_focus_obj(scroll_container_);
}

} // namespace screen
