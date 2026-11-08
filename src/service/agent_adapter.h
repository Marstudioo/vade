/*
 * Vade — pocket companion terminal
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace service {

// ============================================================================
// Agent 适配层（后端通信服务）— 对齐 docs/DESIGN.md §12 统一适配契约：
//   1. send(prompt, history) → 分段回复        （对话主路径）
//   2. fetch_board()         → 拉取任务看板    （可选能力）
//   3. push_note()           → 推送速记        （可选能力）
// 不支持/未配置的能力返回 CapabilityStatus::Unsupported（UI 自动隐藏），不报错不崩溃。
//
// 产品预设：
//   - Dsh          DeepSeek Harness HTTP unary：POST /api/session/prompt 提交
//                  + POST /api/session/wait 轮询增量/完成（路径与字段名可配）。
//   - OpenAiCompat POST /v1/chat/completions，stream=false（覆盖 DeepSeek/智谱/qwen）。
//   - Anthropic    POST /v1/messages，x-api-key + anthropic-version: 2023-06-01。
//   - Custom       HTTP 模板（URL/method/header 模板/body 模板）+ JSON 点路径响应解析。
//
// 线程模型：本层不加锁，假定单线程调用（UI 层将来放工作线程调用本层）。
// send/fetch_board/push_note 均为同步阻塞（含网络等待与 DSH 轮询），本层不触碰 LVGL。
//
// 错误风格：无异常；全部通过 status + error 字符串优雅返回，绝不崩。
//
// 详细配置项（config.extra 键值）与示例见同目录 agent_adapter.md。

enum class AgentKind {
    Dsh,          // DeepSeek Harness unary 轮询（首选）
    OpenAiCompat, // OpenAI 兼容 chat/completions
    Anthropic,    // Anthropic Messages API
    Custom,       // 自定义 HTTP 模板
};

enum class CapabilityStatus {
    Ok,
    Unsupported, // 能力未配置/该适配器不适用：UI 据此隐藏入口
    Error,       // 执行失败：error 字段有原因
};

enum class AgentAbility {
    Send,
    FetchBoard,
    PushNote,
};

struct AgentConfig {
    AgentKind kind = AgentKind::Dsh;
    std::string base_url; // 服务根，如 http://127.0.0.1:8199（末尾斜杠可有可无）
    std::string api_key;  // 可为空；Bearer（Anthropic 为 x-api-key）
    std::string model;    // 可为空
    // 适配器细节参数（键、值均为字符串）：路径模板、响应字段名、超时等，见 agent_adapter.md
    std::map<std::string, std::string> extra;
};

struct ChatMessage {
    std::string role;    // "user" | "assistant" | "system"
    std::string content;
};

struct SendResult {
    CapabilityStatus status = CapabilityStatus::Error;
    std::string error;                   // status == Error 时的原因
    std::vector<std::string> segments;   // 分段回复：DSH 为每次 wait 的增量段，其余适配器单段
    std::string text;                    // 最终完整回复（增量拼接，或权威 text 字段覆盖）
    bool done = false;                   // 是否已完整收完
};

struct BoardResult {
    CapabilityStatus status = CapabilityStatus::Error;
    std::string error;
    std::string raw; // 看板原始响应（JSON/文本），上层自行解读或用 json_path 提取
};

struct NoteResult {
    CapabilityStatus status = CapabilityStatus::Error;
    std::string error;
};

// 增量段回调（可选）：DSH 轮询每收到一个增量段回调一次，供 UI 流式分段渲染。
using SegmentCallback = std::function<void(const std::string& segment)>;

class AgentAdapter {
public:
    AgentAdapter() = default;
    explicit AgentAdapter(const AgentConfig& config);

    void set_config(const AgentConfig& config);
    const AgentConfig& config() const;

    // 能力查询：不支持的能力 UI 自动隐藏。
    bool supports(AgentAbility ability) const;

    // 契约动作 1：send(prompt, history) → 分段回复（同步阻塞）。
    // on_segment 为可选增量回调（默认空），不影响返回值契约。
    SendResult send(const std::string& prompt,
                    const std::vector<ChatMessage>& history,
                    const SegmentCallback& on_segment = nullptr);

    // 契约动作 2：拉取远端任务看板（可选能力，Unsupported 时 UI 隐藏）。
    BoardResult fetch_board();

    // 契约动作 3：推送速记（可选能力，Unsupported 时 UI 隐藏）。
    NoteResult push_note(const std::string& note);

private:
    // HTTP 传输细节（请求构造、头模板解析等）全部是 agent_adapter.cpp 内部自由函数。
    SendResult dsh_send(const std::string& prompt,
                        const std::vector<ChatMessage>& history,
                        const SegmentCallback& on_segment);
    SendResult openai_send(const std::string& prompt,
                           const std::vector<ChatMessage>& history,
                           const SegmentCallback& on_segment);
    SendResult anthropic_send(const std::string& prompt,
                              const std::vector<ChatMessage>& history,
                              const SegmentCallback& on_segment);
    SendResult custom_send(const std::string& prompt,
                           const std::vector<ChatMessage>& history,
                           const SegmentCallback& on_segment);

    std::string extra_get(const std::string& key, const std::string& fallback) const;
    int extra_int(const std::string& key, int fallback) const;

    AgentConfig config_;
};

// 简单 JSON 点路径取值："data.reply"、"choices.0.message.content"（数组用数字下标）。
// 返回标量的字符串形式（字符串去引号，数字/布尔转文本，对象/数组返回空串）；
// ok 指示路径是否命中（可传 nullptr）。
std::string json_path(const std::string& json, const std::string& path, bool* ok = nullptr);

const char* to_string(CapabilityStatus status);
const char* to_string(AgentKind kind);

} // namespace service
