/*
 * Vade — pocket companion terminal
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "subjects.h"
#include "base_model.h"
#include "search_service.h"
#include "pinyin_ime.h"

#include "lvgl.h"

#include <memory>

namespace viewmodel {

class BaseViewModel {
public:
    BaseViewModel();
    ~BaseViewModel();

    BaseViewModel(const BaseViewModel&) = delete;
    BaseViewModel& operator=(const BaseViewModel&) = delete;

    // Subjects
    lv_subject_t* title_subject();
    lv_subject_t* dark_mode_subject();
    lv_subject_t* current_page_subject();
    lv_subject_t* query_subject();
    lv_subject_t* results_version_subject();
    lv_subject_t* selected_result_subject();
    lv_subject_t* reader_version_subject();
    lv_subject_t* font_level_subject();
    lv_subject_t* quit_requested_subject();

    // Page
    model::AppPage current_page() const;
    void show_page(model::AppPage page);
    void show_search_page();
    void show_chat_page();
    void show_tasks_page();
    void show_capture_page();
    void show_settings_page();

    // Theme & prefs
    bool is_dark_mode() const;
    void set_dark_mode(bool enabled);
    void toggle_dark_mode();
    int font_level() const;
    void cycle_font_level();

    // Search
    const std::string& query_text() const;
    void set_query_text(const std::string& text);
    void append_query_char(char c);
    void backspace_query();
    void run_search();
    const std::vector<model::SearchHit>& results() const;
    int selected_result() const;
    void select_next_result();
    void select_prev_result();
    bool open_selected_result();

    // Reader
    const std::string& reader_title() const;
    const std::string& reader_body() const;
    void leave_reader();

    // IME（DESIGN §11A）
    bool load_ime_dict(const std::string& path);
    void load_ime_user_dict(const std::string& path);
    bool load_search_db(const std::string& path);
    bool ime_chinese_mode() const;
    void toggle_ime_mode();
    bool ime_composing_active() const;
    const std::string& ime_composing_text() const;
    const std::vector<std::string>& ime_candidates() const;
    int ime_selected_candidate() const;
    void ime_append_letter(char c);
    void ime_backspace();
    void ime_candidate_next();
    void ime_candidate_prev();
    void ime_commit_selected();
    void ime_commit_first();
    void ime_cancel();
    lv_subject_t* ime_version_subject();

    void request_quit();

private:
    void publish_page();
    static void search_debounce_cb(lv_timer_t* timer);

    model::BaseModel model_;
    std::unique_ptr<service::SearchService> search_service_;
    service::PinyinIme ime_;
    reactive::StringSubject<32> title_subject_;
    reactive::BoolSubject dark_mode_subject_;
    reactive::IntSubject current_page_subject_;
    reactive::StringSubject<48> query_subject_;
    reactive::IntSubject results_version_subject_;
    reactive::IntSubject selected_result_subject_;
    reactive::IntSubject reader_version_subject_;
    reactive::IntSubject font_level_subject_;
    reactive::IntSubject ime_version_subject_;
    reactive::BoolSubject quit_requested_subject_;
    lv_timer_t* search_timer_ = nullptr;
};

} // namespace viewmodel
