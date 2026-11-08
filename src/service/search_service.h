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
};

// 占位实现：返回固定演示结果，验证 UI 流转。
class MockSearchService : public SearchService {
public:
    std::vector<model::SearchHit> query(const std::string& text) override;
};

} // namespace service
