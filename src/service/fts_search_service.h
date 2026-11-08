/*
 * Vade — pocket companion terminal
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "search_service.h"

#include <string>

struct sqlite3;

namespace service {

// SQLite FTS5 真检索（对齐 vade_indexer/query.py 的设备端零分词路径）。
// 库由宿主机 jieba 预分词构建，设备端只查询；0 命中自动 LIKE 兜底。
class FtsSearchService : public SearchService {
public:
    ~FtsSearchService() override;

    bool open(const std::string& db_path);
    bool is_open() const;
    std::vector<model::SearchHit> query(const std::string& text) override;

private:
    std::vector<model::SearchHit> fts_query(const std::string& expr,
                                            const std::string& tag,
                                            const std::string& path);
    std::vector<model::SearchHit> like_query(const std::string& terms,
                                             const std::string& tag,
                                             const std::string& path);
    static std::string desegment(const std::string& s);

    sqlite3* db_ = nullptr;
};

} // namespace service
