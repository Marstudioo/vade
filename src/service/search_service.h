/*
 * Vade — pocket companion terminal
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "base_model.h"

#include <string>

namespace service {

// 检索服务接口：M1 片段1 用 Mock 实现，片段2 换 SQLite FTS5 实现（同接口）。
class SearchService {
public:
    virtual ~SearchService() = default;
    virtual std::vector<model::SearchHit> query(const std::string& text) = 0;
    // 取文档正文（阅读页用）；不支持返回 false
    virtual bool get_doc(const std::string& path,
                         std::string& out_title,
                         std::string& out_body) {
        (void)path; (void)out_title; (void)out_body;
        return false;
    }
};

// 占位实现：返回固定演示结果，验证 UI 流转。
class MockSearchService : public SearchService {
public:
    std::vector<model::SearchHit> query(const std::string& text) override;
    bool get_doc(const std::string& path,
                 std::string& out_title,
                 std::string& out_body) override;
};

} // namespace service
