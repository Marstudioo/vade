/*
 * Vade — pocket companion terminal
 * SPDX-License-Identifier: MIT
 */
#include "search_service.h"

namespace service {

std::vector<model::SearchHit> MockSearchService::query(const std::string& text) {
    std::vector<model::SearchHit> hits;
    if (text.size() < 2) {
        return hits;  // 2 字起搜（DESIGN §11）
    }
    // 演示数据：验证"输入→列表→阅读"三屏流转（片段2 起换真实 FTS5）
    hits.push_back({"CardputerZero硬件与生态",
                    "知识库/AI/M5CardputerZero",
                    "…CardputerZero 是 M5Stack 基于 Raspberry Pi CM0 打造的产品化便携 Linux 电脑…"});
    hits.push_back({"FTS5 检索判决",
                    "知识库/AI/M5CardputerZero/实践经验",
                    "…jieba 预分词 + unicode61 召回≈100%，峰值 139MB，设备端只查询…"});
    hits.push_back({"设计Token规范",
                    "知识库/AI/AI实践项目/UI设计",
                    "…间距系统 4px 基础；暗色模式变量；WCAG 2.1 AA 对比度…"});
    hits.push_back({"语音助手完整架构",
                    "知识库/硬件搭建/07_边缘AI",
                    "…唤醒检测 100-200ms，ASR 2-5s，TTS 300-500ms，总计 3-7 秒…"});
    hits.push_back({"Mars的核心需求",
                    "核心记忆",
                    "…要陪伴感不是分析报告；检索要在用的时候实实在在查到…"});
    return hits;
}

} // namespace service
