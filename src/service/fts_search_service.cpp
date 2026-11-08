/*
 * Vade — pocket companion terminal
 * SPDX-License-Identifier: MIT
 */
#include "fts_search_service.h"

#include "sqlite3.h"

#include <cctype>
#include <vector>

namespace service {

namespace {

constexpr int kSnippetTokens = 24;
constexpr int kMaxHits = 10;
constexpr const char* kSep = "\x01";

std::string fts_quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') {
            out += "\"\"";
        } else {
            out.push_back(c);
        }
    }
    out += "\"";
    return out;
}

std::string lower_ascii(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool starts_with_ci(const std::string& s, const char* prefix) {
    const size_t n = std::char_traits<char>::length(prefix);
    return s.size() >= n && lower_ascii(s.substr(0, n)) == prefix;
}

// 解析查询：tag:/path: 过滤器 + 引号短语（对齐 query.py::parse_query）
void parse_query(const std::string& q,
                 std::vector<std::string>& tokens,
                 std::string& tag,
                 std::string& path) {
    std::string cur;
    bool in_quote = false;
    bool quoted_cur = false;
    auto flush = [&]() {
        if (cur.empty() && !quoted_cur) {
            return;
        }
        if (!quoted_cur && starts_with_ci(cur, "tag:") && cur.size() > 4 && tag.empty()) {
            tag = cur.substr(4);
        } else if (!quoted_cur && starts_with_ci(cur, "path:") && cur.size() > 5 && path.empty()) {
            path = cur.substr(5);
        } else {
            tokens.push_back(cur);
        }
        cur.clear();
        quoted_cur = false;
    };
    for (char c : q) {
        if (c == '"') {
            if (in_quote) {
                flush();
                in_quote = false;
            } else {
                flush();
                in_quote = true;
                quoted_cur = true;
            }
            continue;
        }
        if (!in_quote && (c == ' ' || c == '\t')) {
            flush();
            continue;
        }
        cur.push_back(c);
    }
    flush();
}

} // namespace

FtsSearchService::~FtsSearchService() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool FtsSearchService::open(const std::string& db_path) {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
    if (sqlite3_open_v2(db_path.c_str(), &db_, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        return false;
    }
    return true;
}

bool FtsSearchService::is_open() const { return db_ != nullptr; }

bool FtsSearchService::get_doc(const std::string& path,
                               std::string& out_title,
                               std::string& out_body) {
    if (!db_ || path.empty()) {
        return false;
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT title, body FROM docs WHERE path = ? LIMIT 1",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* body = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        out_title = title ? title : "";
        out_body = body ? body : "";
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

std::string FtsSearchService::desegment(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\x01') {
            continue;
        }
        if (s[i] == '<' && s.compare(i, 7, "</b><b>") == 0) {
            i += 6;
            continue;
        }
        out.push_back(s[i]);
    }
    const size_t b = out.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) {
        return "";
    }
    const size_t e = out.find_last_not_of(" \t\r\n");
    return out.substr(b, e - b + 1);
}

std::vector<model::SearchHit> FtsSearchService::query(const std::string& text) {
    std::vector<model::SearchHit> hits;
    if (!db_ || text.empty()) {
        return hits;
    }

    std::vector<std::string> tokens;
    std::string tag, path;
    parse_query(text, tokens, tag, path);

    if (!tokens.empty()) {
        std::string expr;
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (i > 0) {
                expr += " AND ";
            }
            expr += fts_quote(tokens[i]);
        }
        hits = fts_query(expr, tag, path);
    }

    if (hits.empty()) {
        std::string terms;
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (i > 0) {
                terms += " ";
            }
            terms += tokens[i];
        }
        hits = like_query(terms, tag, path);
    }
    return hits;
}

std::vector<model::SearchHit> FtsSearchService::fts_query(const std::string& expr,
                                                          const std::string& tag,
                                                          const std::string& path) {
    std::vector<model::SearchHit> hits;
    std::string sql =
        "SELECT d.title, d.path, highlight(fts,0,'<b>','</b>'), "
        "snippet(fts,1,'<b>','</b>','…'," + std::to_string(kSnippetTokens) + ") "
        "FROM fts JOIN docs d ON d.doc_id = fts.rowid WHERE fts MATCH ?";
    if (!tag.empty()) {
        sql += " AND d.tags LIKE ?";
    }
    if (!path.empty()) {
        sql += " AND d.path LIKE ?";
    }
    sql += " ORDER BY bm25(fts, 10.0, 1.0) LIMIT " + std::to_string(kMaxHits);

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return hits;
    }
    int idx = 1;
    sqlite3_bind_text(stmt, idx++, expr.c_str(), -1, SQLITE_TRANSIENT);
    if (!tag.empty()) {
        const std::string like = "%" + tag + "%";
        sqlite3_bind_text(stmt, idx++, like.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (!path.empty()) {
        const std::string like = "%" + path + "%";
        sqlite3_bind_text(stmt, idx++, like.c_str(), -1, SQLITE_TRANSIENT);
    }
    while (sqlite3_step(stmt) == SQLITE_ROW && static_cast<int>(hits.size()) < kMaxHits) {
        model::SearchHit hit;
        const char* title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* doc_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* snip = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        hit.title = title ? title : "";
        hit.path = doc_path ? doc_path : "";
        hit.snippet = desegment(snip ? snip : "");
        hits.push_back(std::move(hit));
    }
    sqlite3_finalize(stmt);
    return hits;
}

std::vector<model::SearchHit> FtsSearchService::like_query(const std::string& terms,
                                                           const std::string& tag,
                                                           const std::string& path) {
    std::vector<model::SearchHit> hits;
    if (terms.empty() && tag.empty() && path.empty()) {
        return hits;
    }
    std::string sql = "SELECT title, path, body FROM docs WHERE 1=1";
    if (!terms.empty()) {
        sql += " AND (title LIKE ? OR body LIKE ?)";
    }
    if (!tag.empty()) {
        sql += " AND tags LIKE ?";
    }
    if (!path.empty()) {
        sql += " AND path LIKE ?";
    }
    sql += " LIMIT " + std::to_string(kMaxHits);

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return hits;
    }
    int idx = 1;
    if (!terms.empty()) {
        const std::string like = "%" + terms + "%";
        sqlite3_bind_text(stmt, idx++, like.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, idx++, like.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (!tag.empty()) {
        const std::string like = "%" + tag + "%";
        sqlite3_bind_text(stmt, idx++, like.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (!path.empty()) {
        const std::string like = "%" + path + "%";
        sqlite3_bind_text(stmt, idx++, like.c_str(), -1, SQLITE_TRANSIENT);
    }
    while (sqlite3_step(stmt) == SQLITE_ROW && static_cast<int>(hits.size()) < kMaxHits) {
        model::SearchHit hit;
        const char* title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* doc_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* body = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        hit.title = title ? title : "";
        hit.path = doc_path ? doc_path : "";
        // 原文窗口摘要（对齐 query.py::like_snippet 的窗口逻辑，简化为纯文本）
        const std::string b = body ? body : "";
        const std::string low = lower_ascii(b);
        const std::string needle = lower_ascii(terms);
        size_t pos = needle.empty() ? std::string::npos : low.find(needle);
        if (pos == std::string::npos) {
            hit.snippet = "…" + b.substr(0, 80);
        } else {
            const size_t start = pos > 60 ? pos - 60 : 0;
            const size_t end = std::min(b.size(), pos + needle.size() + 120);
            hit.snippet = (start > 0 ? "…" : "") + b.substr(start, end - start) +
                          (end < b.size() ? "…" : "");
        }
        hits.push_back(std::move(hit));
    }
    sqlite3_finalize(stmt);
    return hits;
}

} // namespace service
