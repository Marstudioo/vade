/*
 * Vade — pocket companion terminal
 * SPDX-License-Identifier: MIT
 */
#include "base_model.h"

namespace model {

const char* BaseModel::app_title() const { return "Vade"; }

bool BaseModel::dark_mode() const { return dark_mode_; }
void BaseModel::set_dark_mode(bool enabled) { dark_mode_ = enabled; }
void BaseModel::toggle_dark_mode() { dark_mode_ = !dark_mode_; }

AppPage BaseModel::current_page() const { return current_page_; }
void BaseModel::set_current_page(AppPage page) { current_page_ = page; }

const std::string& BaseModel::query() const { return query_; }
void BaseModel::set_query(std::string query) { query_ = std::move(query); }

const std::vector<SearchHit>& BaseModel::results() const { return results_; }
void BaseModel::set_results(std::vector<SearchHit> results) {
    results_ = std::move(results);
    selected_result_ = 0;
}

int BaseModel::selected_result() const { return selected_result_; }
void BaseModel::set_selected_result(int index) {
    if (results_.empty()) {
        selected_result_ = 0;
        return;
    }
    if (index < 0) index = 0;
    if (index >= static_cast<int>(results_.size())) index = static_cast<int>(results_.size()) - 1;
    selected_result_ = index;
}

int BaseModel::result_count() const { return static_cast<int>(results_.size()); }

void BaseModel::open_reader(std::string title, std::string body) {
    reader_title_ = std::move(title);
    reader_body_ = std::move(body);
}

const std::string& BaseModel::reader_title() const { return reader_title_; }
const std::string& BaseModel::reader_body() const { return reader_body_; }

int BaseModel::font_level() const { return font_level_; }
void BaseModel::set_font_level(int level) {
    if (level < 0) level = 0;
    if (level > 2) level = 2;
    font_level_ = level;
}

} // namespace model
