/*
 * Vade — pocket companion terminal
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <string>
#include <vector>

namespace service {

// 拼音输入法引擎（DESIGN §11A）：字母组拼音 → 候选词（词频序）→ 上屏。
// 词库格式：每行 "pinyin\tword\tfreq"（拼音为无调小写连写，如 zhishidian）。
class PinyinIme {
public:
    bool load_dict(const std::string& path);

    bool chinese_mode() const;
    void set_chinese_mode(bool enabled);
    void toggle_mode();

    bool composing() const;
    const std::string& composing_text() const;
    void append_letter(char c);
    void backspace();

    const std::vector<std::string>& candidates() const;
    int selected_candidate() const;
    void select_next_candidate();
    void select_prev_candidate();
    std::string commit_selected();   // 上屏高亮候选；无候选时上屏原始拼音
    std::string commit_first();      // 上屏首选
    void cancel();

private:
    void refresh_candidates();

    struct Entry {
        std::string pinyin;
        std::string word;
        int freq = 0;
    };

    bool loaded_ = false;
    bool chinese_mode_ = true;
    std::string composing_;
    std::vector<Entry> entries_;          // 按 pinyin 字典序
    std::vector<std::string> candidates_;
    int selected_ = 0;
};

} // namespace service
