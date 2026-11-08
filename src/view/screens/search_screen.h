/*
 * Vade — pocket companion terminal
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "base_screen.h"

namespace screen {

class SearchScreen : public BaseScreen {
public:
    using BaseScreen::BaseScreen;
    ~SearchScreen() override;

protected:
    void build_content(lv_obj_t* content) override;

private:
    static void results_changed_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void selection_changed_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void theme_changed_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void ime_changed_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void blink_timer_cb(lv_timer_t* timer);
    static void cleanup_cb(lv_event_t* event);

    void rebuild_results();
    void refresh_selection();
    void refresh_query_line();
    void refresh_ime_ui();
    void stop_blink();

    lv_obj_t* query_label_ = nullptr;
    lv_obj_t* mode_badge_ = nullptr;
    lv_obj_t* candidate_row_ = nullptr;
    lv_obj_t* candidate_labels_[5] = {nullptr};
    lv_obj_t* results_container_ = nullptr;
    lv_obj_t* rows_[20] = {nullptr};
    int row_count_ = 0;
    bool caret_on_ = true;
    lv_timer_t* blink_timer_ = nullptr;
    lv_observer_t* results_observer_ = nullptr;
    lv_observer_t* selection_observer_ = nullptr;
    lv_observer_t* theme_observer_ = nullptr;
    lv_observer_t* ime_observer_ = nullptr;
};

} // namespace screen
