/*
 * Vade — pocket companion terminal
 * SPDX-License-Identifier: MIT
 */
#include "base_viewmodel.h"
#include "fts_search_service.h"
#include "logger.h"

namespace viewmodel {

BaseViewModel::BaseViewModel()
    : search_service_(std::make_unique<service::MockSearchService>()),
      title_subject_("Vade"),
      dark_mode_subject_(true),
      current_page_subject_(static_cast<int>(model::AppPage::Search)),
      query_subject_(""),
      results_version_subject_(0),
      selected_result_subject_(0),
      reader_version_subject_(0),
      font_level_subject_(1),
      ime_version_subject_(0),
      quit_requested_subject_(false) {
    model_.set_dark_mode(true);
}

BaseViewModel::~BaseViewModel() = default;

lv_subject_t* BaseViewModel::title_subject() { return title_subject_.native(); }
lv_subject_t* BaseViewModel::dark_mode_subject() { return dark_mode_subject_.native(); }
lv_subject_t* BaseViewModel::current_page_subject() { return current_page_subject_.native(); }
lv_subject_t* BaseViewModel::query_subject() { return query_subject_.native(); }
lv_subject_t* BaseViewModel::results_version_subject() { return results_version_subject_.native(); }
lv_subject_t* BaseViewModel::selected_result_subject() { return selected_result_subject_.native(); }
lv_subject_t* BaseViewModel::reader_version_subject() { return reader_version_subject_.native(); }
lv_subject_t* BaseViewModel::font_level_subject() { return font_level_subject_.native(); }
lv_subject_t* BaseViewModel::quit_requested_subject() { return quit_requested_subject_.native(); }

model::AppPage BaseViewModel::current_page() const { return model_.current_page(); }

void BaseViewModel::publish_page() {
    current_page_subject_.set(static_cast<int>(model_.current_page()));
}

void BaseViewModel::show_page(model::AppPage page) {
    model_.set_current_page(page);
    publish_page();
}

void BaseViewModel::show_search_page() { show_page(model::AppPage::Search); }
void BaseViewModel::show_chat_page() { show_page(model::AppPage::Chat); }
void BaseViewModel::show_tasks_page() { show_page(model::AppPage::Tasks); }
void BaseViewModel::show_capture_page() { show_page(model::AppPage::Capture); }
void BaseViewModel::show_settings_page() { show_page(model::AppPage::Settings); }

bool BaseViewModel::is_dark_mode() const { return model_.dark_mode(); }

void BaseViewModel::set_dark_mode(bool enabled) {
    model_.set_dark_mode(enabled);
    dark_mode_subject_.set(enabled);
}

void BaseViewModel::toggle_dark_mode() { set_dark_mode(!model_.dark_mode()); }

int BaseViewModel::font_level() const { return model_.font_level(); }

void BaseViewModel::cycle_font_level() {
    model_.set_font_level((model_.font_level() + 1) % 3);
    font_level_subject_.set(model_.font_level());
    reader_version_subject_.set(reader_version_subject_.value() + 1);
}

const std::string& BaseViewModel::query_text() const { return model_.query(); }

void BaseViewModel::set_query_text(const std::string& text) {
    model_.set_query(text);
    query_subject_.set(text.c_str());
    // 200ms 防抖搜索（DESIGN §11）：连续打字只在停手后搜一次
    if (!search_timer_) {
        search_timer_ = lv_timer_create(search_debounce_cb, 200, this);
    } else {
        lv_timer_resume(search_timer_);
        lv_timer_reset(search_timer_);
    }
}

void BaseViewModel::search_debounce_cb(lv_timer_t* timer) {
    auto* self = static_cast<BaseViewModel*>(lv_timer_get_user_data(timer));
    if (self) {
        lv_timer_pause(timer);
        self->run_search();
    }
}

void BaseViewModel::append_query_char(char c) {
    std::string q = model_.query();
    q.push_back(c);
    set_query_text(q);
}

void BaseViewModel::backspace_query() {
    std::string q = model_.query();
    if (!q.empty()) {
        q.pop_back();
    }
    set_query_text(q);
}

void BaseViewModel::run_search() {
    model_.set_results(search_service_->query(model_.query()));
    selected_result_subject_.set(model_.selected_result());
    results_version_subject_.set(results_version_subject_.value() + 1);
    LOG_INFO("search: '{}' -> {} hits", model_.query(), model_.result_count());
}

const std::vector<model::SearchHit>& BaseViewModel::results() const { return model_.results(); }
int BaseViewModel::selected_result() const { return model_.selected_result(); }

void BaseViewModel::select_next_result() {
    model_.set_selected_result(model_.selected_result() + 1);
    selected_result_subject_.set(model_.selected_result());
}

void BaseViewModel::select_prev_result() {
    model_.set_selected_result(model_.selected_result() - 1);
    selected_result_subject_.set(model_.selected_result());
}

bool BaseViewModel::open_selected_result() {
    const auto& hits = model_.results();
    int idx = model_.selected_result();
    if (idx < 0 || idx >= static_cast<int>(hits.size())) {
        return false;
    }
    const auto& hit = hits[idx];
    std::string title, body;
    if (search_service_->get_doc(hit.path, title, body)) {
        // 超长文档截断保护（LVGL 长标签性能红线，DESIGN §5）
        constexpr std::size_t kMaxBody = 6000;
        if (body.size() > kMaxBody) {
            body = body.substr(0, kMaxBody) + "\n\n…（全文约 " +
                   std::to_string(body.size()) + " 字节，长文完整阅读后续版本支持）";
        }
        model_.open_reader(title.empty() ? hit.title : title, body);
    } else {
        model_.open_reader(hit.title, hit.snippet + "\n\n（正文读取失败，暂显示摘要）");
    }
    show_page(model::AppPage::Reader);
    reader_version_subject_.set(reader_version_subject_.value() + 1);
    LOG_INFO("reader open: '{}' ({} bytes)", model_.reader_title(), model_.reader_body().size());
    return true;
}

const std::string& BaseViewModel::reader_title() const { return model_.reader_title(); }
const std::string& BaseViewModel::reader_body() const { return model_.reader_body(); }

void BaseViewModel::leave_reader() { show_page(model::AppPage::Search); }

// ── IME ─────────────────────────────────────────────────────────────
bool BaseViewModel::load_ime_dict(const std::string& path) { return ime_.load_dict(path); }

bool BaseViewModel::load_search_db(const std::string& path) {
    auto fts = std::make_unique<service::FtsSearchService>();
    if (!fts->open(path)) {
        return false;
    }
    search_service_ = std::move(fts);
    return true;
}
bool BaseViewModel::ime_chinese_mode() const { return ime_.chinese_mode(); }
bool BaseViewModel::ime_composing_active() const { return ime_.composing(); }
const std::string& BaseViewModel::ime_composing_text() const { return ime_.composing_text(); }
const std::vector<std::string>& BaseViewModel::ime_candidates() const { return ime_.candidates(); }
int BaseViewModel::ime_selected_candidate() const { return ime_.selected_candidate(); }
lv_subject_t* BaseViewModel::ime_version_subject() { return ime_version_subject_.native(); }

void BaseViewModel::toggle_ime_mode() {
    ime_.toggle_mode();
    ime_version_subject_.set(ime_version_subject_.value() + 1);
}

void BaseViewModel::ime_append_letter(char c) {
    ime_.append_letter(c);
    ime_version_subject_.set(ime_version_subject_.value() + 1);
}

void BaseViewModel::ime_backspace() {
    ime_.backspace();
    ime_version_subject_.set(ime_version_subject_.value() + 1);
}

void BaseViewModel::ime_candidate_next() {
    ime_.select_next_candidate();
    ime_version_subject_.set(ime_version_subject_.value() + 1);
}

void BaseViewModel::ime_candidate_prev() {
    ime_.select_prev_candidate();
    ime_version_subject_.set(ime_version_subject_.value() + 1);
}

void BaseViewModel::ime_commit_selected() {
    const std::string word = ime_.commit_selected();
    ime_version_subject_.set(ime_version_subject_.value() + 1);
    if (!word.empty()) {
        LOG_INFO("ime commit: '{}'", word);
        set_query_text(query_text() + word);
    }
}

void BaseViewModel::ime_commit_first() {
    const std::string word = ime_.commit_first();
    ime_version_subject_.set(ime_version_subject_.value() + 1);
    if (!word.empty()) {
        set_query_text(query_text() + word);
    }
}

void BaseViewModel::ime_cancel() {
    ime_.cancel();
    ime_version_subject_.set(ime_version_subject_.value() + 1);
}

void BaseViewModel::request_quit() { quit_requested_subject_.set(true); }

} // namespace viewmodel
