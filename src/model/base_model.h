/*
 * Vade — pocket companion terminal
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <string>
#include <vector>

namespace model {

enum class AppPage {
    Search   = 0,
    Chat     = 1,
    Tasks    = 2,
    Capture  = 3,
    Settings = 4,
    Reader   = 5,
};

struct SearchHit {
    std::string title;
    std::string path;
    std::string snippet;
};

class BaseModel {
public:
    const char* app_title() const;

    bool dark_mode() const;
    void set_dark_mode(bool enabled);
    void toggle_dark_mode();

    AppPage current_page() const;
    void set_current_page(AppPage page);

    // Search state
    const std::string& query() const;
    void set_query(std::string query);
    const std::vector<SearchHit>& results() const;
    void set_results(std::vector<SearchHit> results);
    int selected_result() const;
    void set_selected_result(int index);
    int result_count() const;

    // Reader state
    void open_reader(std::string title, std::string body);
    const std::string& reader_title() const;
    const std::string& reader_body() const;

    // Preferences
    int font_level() const;          // 0 small, 1 medium, 2 large
    void set_font_level(int level);

private:
    bool dark_mode_ = true;
    AppPage current_page_ = AppPage::Search;
    std::string query_;
    std::vector<SearchHit> results_;
    int selected_result_ = 0;
    std::string reader_title_;
    std::string reader_body_;
    int font_level_ = 1;
};

} // namespace model
