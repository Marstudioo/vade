/*
 * Vade — pocket companion terminal
 * SPDX-License-Identifier: MIT
 */
#include "search_screen.h"

#include "bindings.h"
#include "theme.h"

namespace screen {

namespace {
constexpr int kMaxRows = 20;
constexpr int kMaxVisibleCandidates = 5;
constexpr uint32_t kBlinkMs = 530;
constexpr uint32_t kAccent = 0x4D6BFE;  // DESIGN §2 primary/accent

// 布局（内容区实测 110px = 170 - 30 标题 - 30 导航）
constexpr int32_t kQueryY = 1;
constexpr int32_t kQueryH = 22;
constexpr int32_t kCandidateY = 25;
constexpr int32_t kCandidateH = 20;
constexpr int32_t kResultsYIdle = 25;
constexpr int32_t kResultsHIdle = 83;
constexpr int32_t kResultsYComposing = 47;
constexpr int32_t kResultsHComposing = 61;
constexpr int32_t kRowH = 34;
}

SearchScreen::~SearchScreen() {
    stop_blink();
}

void SearchScreen::stop_blink() {
    if (blink_timer_) {
        lv_timer_del(blink_timer_);
        blink_timer_ = nullptr;
    }
}

void SearchScreen::cleanup_cb(lv_event_t* event) {
    auto* self = static_cast<SearchScreen*>(lv_event_get_user_data(event));
    if (self) {
        self->stop_blink();
    }
}

void SearchScreen::blink_timer_cb(lv_timer_t* timer) {
    auto* self = static_cast<SearchScreen*>(lv_timer_get_user_data(timer));
    if (!self || !self->query_label_ || !lv_obj_is_valid(self->query_label_)) {
        return;
    }
    self->caret_on_ = !self->caret_on_;
    self->refresh_query_line();
}

void SearchScreen::refresh_query_line() {
    if (!query_label_ || !lv_obj_is_valid(query_label_)) {
        return;
    }
    std::string text = view_model().query_text();
    text += view_model().ime_composing_text();
    text += caret_on_ ? "_" : " ";
    lv_label_set_text(query_label_, text.c_str());
}

void SearchScreen::build_content(lv_obj_t* content) {
    // 查询行：Surface 底 + 文本尾部闪烁光标 + 中/英角标
    auto* query_row = lv_obj_create(content);
    lv_obj_remove_style_all(query_row);
    lv_obj_set_size(query_row, 304, kQueryH);
    lv_obj_align(query_row, LV_ALIGN_TOP_MID, 0, kQueryY);
    lv_obj_clear_flag(query_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(query_row, 6, 0);
    lv_obj_set_style_pad_hor(query_row, 6, 0);
    lv_obj_set_style_bg_opa(query_row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(query_row, 1, 0);
    const auto colors = view::palette(view_model().is_dark_mode());
    lv_obj_set_style_border_color(query_row, colors.border, 0);
    reactive::bind_theme(query_row, view_model().dark_mode_subject(), reactive::ThemeRole::Surface);
    lv_obj_add_event_cb(query_row, cleanup_cb, LV_EVENT_DELETE, this);

    query_label_ = lv_label_create(query_row);
    auto* query_font = assets().load_standard_font(15, app::StandardFontWeight::Regular);
    if (query_font) {
        lv_obj_set_style_text_font(query_label_, query_font, 0);
    }
    lv_label_set_long_mode(query_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(query_label_, 256);
    lv_obj_align(query_label_, LV_ALIGN_LEFT_MID, 0, 0);
    reactive::bind_theme(query_label_, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
    refresh_query_line();

    mode_badge_ = lv_label_create(query_row);
    auto* badge_font = assets().load_standard_font(12, app::StandardFontWeight::Bold);
    if (badge_font) {
        lv_obj_set_style_text_font(mode_badge_, badge_font, 0);
    }
    lv_obj_align(mode_badge_, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_text_color(mode_badge_, lv_color_hex(kAccent), 0);

    blink_timer_ = lv_timer_create(blink_timer_cb, kBlinkMs, this);

    // 候选条（组词时出现）
    candidate_row_ = lv_obj_create(content);
    lv_obj_remove_style_all(candidate_row_);
    lv_obj_set_size(candidate_row_, 304, kCandidateH);
    lv_obj_align(candidate_row_, LV_ALIGN_TOP_MID, 0, kCandidateY);
    lv_obj_set_flex_flow(candidate_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(candidate_row_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(candidate_row_, 8, 0);
    lv_obj_set_style_pad_hor(candidate_row_, 4, 0);
    lv_obj_clear_flag(candidate_row_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(candidate_row_, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < kMaxVisibleCandidates; i++) {
        auto* label = lv_label_create(candidate_row_);
        lv_label_set_text(label, "");
        if (badge_font) {
            lv_obj_set_style_text_font(label, badge_font, 0);
        }
        reactive::bind_theme(label, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
        candidate_labels_[i] = label;
    }

    // 结果区
    results_container_ = lv_obj_create(content);
    lv_obj_remove_style_all(results_container_);
    lv_obj_set_size(results_container_, 304, kResultsHIdle);
    lv_obj_align(results_container_, LV_ALIGN_TOP_MID, 0, kResultsYIdle);
    lv_obj_set_flex_flow(results_container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(results_container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(results_container_, 2, 0);
    lv_obj_set_scrollbar_mode(results_container_, LV_SCROLLBAR_MODE_AUTO);
    reactive::bind_theme(results_container_, view_model().dark_mode_subject(), reactive::ThemeRole::Screen);

    results_observer_ = reactive::observe_obj(results_container_, view_model().results_version_subject(),
                                              results_changed_cb, this);
    selection_observer_ = reactive::observe_obj(results_container_, view_model().selected_result_subject(),
                                                selection_changed_cb, this);
    theme_observer_ = reactive::observe_obj(results_container_, view_model().dark_mode_subject(),
                                            theme_changed_cb, this);
    ime_observer_ = reactive::observe_obj(results_container_, view_model().ime_version_subject(),
                                          ime_changed_cb, this);

    refresh_ime_ui();
    rebuild_results();
}

void SearchScreen::results_changed_cb(lv_observer_t* observer, lv_subject_t* subject) {
    LV_UNUSED(subject);
    auto* self = static_cast<SearchScreen*>(lv_observer_get_user_data(observer));
    if (self) {
        self->rebuild_results();
    }
}

void SearchScreen::selection_changed_cb(lv_observer_t* observer, lv_subject_t* subject) {
    LV_UNUSED(subject);
    auto* self = static_cast<SearchScreen*>(lv_observer_get_user_data(observer));
    if (self) {
        self->refresh_selection();
    }
}

void SearchScreen::theme_changed_cb(lv_observer_t* observer, lv_subject_t* subject) {
    LV_UNUSED(subject);
    auto* self = static_cast<SearchScreen*>(lv_observer_get_user_data(observer));
    if (self) {
        self->rebuild_results();
        self->refresh_query_line();
    }
}

void SearchScreen::ime_changed_cb(lv_observer_t* observer, lv_subject_t* subject) {
    LV_UNUSED(subject);
    auto* self = static_cast<SearchScreen*>(lv_observer_get_user_data(observer));
    if (self) {
        self->refresh_ime_ui();
        self->refresh_query_line();
    }
}

void SearchScreen::refresh_ime_ui() {
    const bool composing = view_model().ime_composing_active();

    // 中/英角标
    if (mode_badge_ && lv_obj_is_valid(mode_badge_)) {
        lv_label_set_text(mode_badge_, view_model().ime_chinese_mode() ? "\u4E2D" : "\u82F1");  // 中 / 英
    }

    // 候选条显隐 + 结果区让位
    if (candidate_row_ && lv_obj_is_valid(candidate_row_)) {
        if (composing) {
            lv_obj_remove_flag(candidate_row_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(candidate_row_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (results_container_ && lv_obj_is_valid(results_container_)) {
        const int32_t y = composing ? kResultsYComposing : kResultsYIdle;
        const int32_t h = composing ? kResultsHComposing : kResultsHIdle;
        lv_obj_set_size(results_container_, 304, h);
        lv_obj_set_pos(results_container_, (304 - 304) / 2, y);
    }

    // 候选内容：选中项用 accent 色块+白字强区分；角标带位置指示 k/n
    const auto& candidates = view_model().ime_candidates();
    const int selected = view_model().ime_selected_candidate();
    if (mode_badge_ && lv_obj_is_valid(mode_badge_)) {
        if (composing && !candidates.empty()) {
            std::string badge = view_model().ime_chinese_mode() ? "\u4E2D" : "\u82F1";
            badge += " " + std::to_string(selected + 1) + "/" + std::to_string(candidates.size());
            lv_label_set_text(mode_badge_, badge.c_str());
        } else {
            lv_label_set_text(mode_badge_, view_model().ime_chinese_mode() ? "\u4E2D" : "\u82F1");
        }
    }
    for (int i = 0; i < kMaxVisibleCandidates; i++) {
        if (!candidate_labels_[i] || !lv_obj_is_valid(candidate_labels_[i])) {
            continue;
        }
        if (i < static_cast<int>(candidates.size())) {
            std::string text = std::to_string(i + 1);
            text += candidates[i];
            lv_label_set_text(candidate_labels_[i], text.c_str());
            lv_obj_remove_flag(candidate_labels_[i], LV_OBJ_FLAG_HIDDEN);
            reactive::bind_theme(candidate_labels_[i], view_model().dark_mode_subject(), reactive::ThemeRole::Text);
            if (i == selected) {
                // 强区分：accent 色块 + 白字 + 圆角
                lv_obj_set_style_bg_color(candidate_labels_[i], lv_color_hex(kAccent), 0);
                lv_obj_set_style_bg_opa(candidate_labels_[i], LV_OPA_COVER, 0);
                lv_obj_set_style_radius(candidate_labels_[i], 4, 0);
                lv_obj_set_style_pad_hor(candidate_labels_[i], 4, 0);
                lv_obj_set_style_text_color(candidate_labels_[i], lv_color_hex(0xFFFFFF), 0);
            } else {
                lv_obj_set_style_bg_opa(candidate_labels_[i], LV_OPA_TRANSP, 0);
            }
        } else {
            lv_obj_add_flag(candidate_labels_[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void SearchScreen::rebuild_results() {
    if (!results_container_ || !lv_obj_is_valid(results_container_)) {
        return;
    }
    lv_obj_clean(results_container_);
    row_count_ = 0;

    const auto colors = view::palette(view_model().is_dark_mode());
    const auto& hits = view_model().results();
    auto* hint_font = assets().load_standard_font(12, app::StandardFontWeight::Regular);

    if (hits.empty()) {
        auto* hint = lv_label_create(results_container_);
        lv_label_set_text(hint, "\u8F93\u5165\u5173\u952E\u8BCD\u5F00\u59CB\u68C0\u7D22  \u00B7  fn+H \u5E2E\u52A9");
        if (hint_font) {
            lv_obj_set_style_text_font(hint, hint_font, 0);
        }
        lv_obj_set_width(hint, 292);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_opa(hint, LV_OPA_50, 0);
        reactive::bind_theme(hint, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
        return;
    }

    auto* title_font = assets().load_standard_font(14, app::StandardFontWeight::Bold);
    auto* snippet_font = hint_font;

    for (size_t i = 0; i < hits.size() && row_count_ < kMaxRows; i++) {
        auto* row = lv_obj_create(results_container_);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 300, kRowH);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, colors.border, 0);
        reactive::bind_theme(row, view_model().dark_mode_subject(), reactive::ThemeRole::Surface);

        auto* title = lv_label_create(row);
        lv_label_set_text(title, hits[i].title.c_str());
        if (title_font) {
            lv_obj_set_style_text_font(title, title_font, 0);
        }
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_obj_set_width(title, 288);
        lv_obj_set_pos(title, 6, 2);
        reactive::bind_theme(title, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

        auto* snippet = lv_label_create(row);
        lv_label_set_text(snippet, hits[i].snippet.c_str());
        if (snippet_font) {
            lv_obj_set_style_text_font(snippet, snippet_font, 0);
        }
        lv_label_set_long_mode(snippet, LV_LABEL_LONG_DOT);
        lv_obj_set_width(snippet, 288);
        lv_obj_set_pos(snippet, 6, 18);
        lv_obj_set_style_text_opa(snippet, LV_OPA_60, 0);
        reactive::bind_theme(snippet, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

        rows_[row_count_] = row;
        row_count_++;
    }
    refresh_selection();
}

void SearchScreen::refresh_selection() {
    const int selected = view_model().selected_result();
    for (int i = 0; i < row_count_; i++) {
        if (!rows_[i] || !lv_obj_is_valid(rows_[i])) {
            continue;
        }
        const bool is_selected = i == selected;
        lv_obj_set_style_border_color(rows_[i],
                                      is_selected ? lv_color_hex(kAccent) : view::palette(view_model().is_dark_mode()).border,
                                      0);
        lv_obj_set_style_border_width(rows_[i], is_selected ? 2 : 1, 0);
        if (is_selected) {
            lv_obj_scroll_to_view(rows_[i], LV_ANIM_OFF);
        }
    }
}

} // namespace screen
