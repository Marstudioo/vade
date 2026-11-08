/*
 * Vade — pocket companion terminal
 * SPDX-License-Identifier: MIT
 */
#include "pinyin_ime.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace service {

bool PinyinIme::load_dict(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    entries_.clear();
    std::string line;
    while (std::getline(in, line)) {
        const auto t1 = line.find('\t');
        if (t1 == std::string::npos) {
            continue;
        }
        const auto t2 = line.find('\t', t1 + 1);
        Entry e;
        e.pinyin = line.substr(0, t1);
        e.word = line.substr(t1 + 1, (t2 == std::string::npos) ? std::string::npos : t2 - t1 - 1);
        if (t2 != std::string::npos) {
            e.freq = std::atoi(line.c_str() + t2 + 1);
        }
        if (!e.pinyin.empty() && !e.word.empty()) {
            entries_.push_back(std::move(e));
        }
    }
    std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
        return a.pinyin < b.pinyin || (a.pinyin == b.pinyin && a.freq > b.freq);
    });
    loaded_ = !entries_.empty();
    return loaded_;
}

bool PinyinIme::chinese_mode() const { return chinese_mode_; }
void PinyinIme::set_chinese_mode(bool enabled) {
    chinese_mode_ = enabled;
    if (!enabled) {
        cancel();
    }
}
void PinyinIme::toggle_mode() { set_chinese_mode(!chinese_mode_); }

bool PinyinIme::composing() const { return !composing_.empty(); }
const std::string& PinyinIme::composing_text() const { return composing_; }

void PinyinIme::append_letter(char c) {
    composing_.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    refresh_candidates();
}

void PinyinIme::backspace() {
    if (!composing_.empty()) {
        composing_.pop_back();
        refresh_candidates();
    }
}

const std::vector<std::string>& PinyinIme::candidates() const { return candidates_; }
int PinyinIme::selected_candidate() const { return selected_; }

void PinyinIme::select_next_candidate() {
    if (!candidates_.empty()) {
        selected_ = (selected_ + 1) % static_cast<int>(candidates_.size());
    }
}

void PinyinIme::select_prev_candidate() {
    if (!candidates_.empty()) {
        selected_ = (selected_ + static_cast<int>(candidates_.size()) - 1) % static_cast<int>(candidates_.size());
    }
}

std::string PinyinIme::commit_selected() {
    if (composing_.empty()) {
        return "";
    }
    std::string word = composing_;
    if (selected_ >= 0 && selected_ < static_cast<int>(candidates_.size())) {
        word = candidates_[selected_];
    }
    cancel();
    return word;
}

std::string PinyinIme::commit_first() {
    selected_ = 0;
    return commit_selected();
}

void PinyinIme::cancel() {
    composing_.clear();
    candidates_.clear();
    selected_ = 0;
}

void PinyinIme::set_user_dict_path(const std::string& path) {
    user_dict_path_ = path;
    load_user_dict();
}

void PinyinIme::load_user_dict() {
    user_counts_.clear();
    std::ifstream in(user_dict_path_);
    if (!in) {
        return;
    }
    std::string line;
    while (std::getline(in, line)) {
        const auto tab = line.find('\t');
        if (tab == std::string::npos) {
            continue;
        }
        user_counts_[line.substr(0, tab)] = std::atoi(line.c_str() + tab + 1);
    }
}

void PinyinIme::save_user_dict() {
    if (user_dict_path_.empty()) {
        return;
    }
    std::ofstream out(user_dict_path_, std::ios::trunc);
    if (!out) {
        return;
    }
    for (const auto& [word, count] : user_counts_) {
        out << word << '\t' << count << '\n';
    }
}

void PinyinIme::note_selection(const std::string& word) {
    if (word.empty()) {
        return;
    }
    user_counts_[word]++;
    save_user_dict();
}

void PinyinIme::refresh_candidates() {
    candidates_.clear();
    selected_ = 0;
    if (composing_.empty()) {
        return;
    }

    auto it = std::lower_bound(entries_.begin(), entries_.end(), composing_,
                               [](const Entry& e, const std::string& s) { return e.pinyin < s; });

    // 1) 整串匹配（全音节词，词频序）
    for (auto p = it; p != entries_.end() && p->pinyin == composing_ && candidates_.size() < 30; ++p) {
        candidates_.push_back(p->word);
    }
    // 2) 前缀匹配（更长拼音的首词，每键取词频最高一个）
    std::string last_key;
    for (auto p = it; p != entries_.end() && candidates_.size() < 48; ++p) {
        if (p->pinyin.size() <= composing_.size() ||
            p->pinyin.compare(0, composing_.size(), composing_) != 0) {
            break;
        }
        if (p->pinyin != last_key) {
            last_key = p->pinyin;
            candidates_.push_back(p->word);
        }
    }
    // 3) 无候选时保留原始拼音（可上屏英文/拼音原文）
    if (candidates_.empty()) {
        candidates_.push_back(composing_);
        return;
    }
    // 4) 词频自适应：用过的词上浮（稳定排序，同频保持原有顺序）
    std::stable_sort(candidates_.begin(), candidates_.end(),
                     [this](const std::string& a, const std::string& b) {
                         const int ca = user_counts_.count(a) ? user_counts_.at(a) : 0;
                         const int cb = user_counts_.count(b) ? user_counts_.at(b) : 0;
                         return ca > cb;
                     });
}

} // namespace service
