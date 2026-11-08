# Agent 适配层（agent_adapter）接口说明

> 对齐 `docs/DESIGN.md` §12「Agent 适配器设计」统一契约。只做后端通信，不碰 LVGL；
> 同步阻塞调用，UI 层放工作线程。

## 1. 统一契约（三个动作）

```cpp
namespace service {

class AgentAdapter {
public:
    explicit AgentAdapter(const AgentConfig& config);
    void set_config(const AgentConfig& config);   // 后端管理器多配置切换
    const AgentConfig& config() const;

    bool supports(AgentAbility ability) const;    // 能力查询：不支持 → UI 自动隐藏

    // 动作 1：对话主路径（同步阻塞）
    SendResult send(const std::string& prompt,
                    const std::vector<ChatMessage>& history,
                    const SegmentCallback& on_segment = nullptr);

    // 动作 2：拉取任务看板（可选能力）
    BoardResult fetch_board();

    // 动作 3：推送速记（可选能力）
    NoteResult push_note(const std::string& note);
};

} // namespace service
```

- `send(prompt, history) → 分段回复`：`SendResult.segments` 为分段（DSH 适配器 = 每次
  wait 的增量段，可配合 `on_segment` 回调做流式渲染；OpenAI/Anthropic/Custom 单段），
  `SendResult.text` 为最终完整回复，`done` 表示是否收完。
- `fetch_board()` / `push_note()`：不支持或未配置时返回
  `CapabilityStatus::Unsupported`（不是报错），UI 据 `supports()` 隐藏入口。
- 错误一律 `CapabilityStatus::Error` + `error` 字符串（超时 / 非 2xx / JSON 解析失败
  均优雅返回），无异常、不崩溃。
- 辅助：`service::json_path(json, "data.reply", &ok)` 简单 JSON 点路径取值
  （数组用数字下标，如 `"choices.0.message.content"`）；`to_string(status/kind)`。

**线程模型**：本层不加锁，假定单线程调用；`send/fetch_board/push_note` 均为同步阻塞
（含网络等待与 DSH 轮询），UI 层须放工作线程调用。

## 2. 配置结构

```cpp
struct AgentConfig {
    AgentKind kind;      // Dsh | OpenAiCompat | Anthropic | Custom
    std::string base_url; // 服务根，末尾斜杠可有可无
    std::string api_key;  // 可空：Bearer（Anthropic 为 x-api-key）
    std::string model;    // 可空
    std::map<std::string, std::string> extra; // 适配器细节参数，键值均为字符串
};
```

通用 `extra` 键：

| 键 | 默认 | 说明 |
|---|---|---|
| `http_timeout_ms` | `10000` | 单次 HTTP 超时（毫秒） |
| `insecure_tls` | `0` | `1`/`true` 关闭 TLS 证书校验（设备无 CA 包时救急，慎用） |

## 3. 各适配器配置示例

### 3.1 DSH（首选）— unary 轮询

```json
{
  "kind": "dsh",
  "base_url": "http://127.0.0.1:8199",
  "api_key": "",
  "model": "",
  "extra": {
    "prompt_path": "/api/session/prompt",
    "wait_path": "/api/session/wait",
    "poll_interval_ms": "500",
    "send_timeout_s": "120"
  }
}
```

流程：`POST prompt_path`，body `{"prompt": "...", "history": [{role,content}...], "model": "..."}`
→ 响应取 `session_id`；然后循环 `POST wait_path`，body `{"session_id": "..."}`
→ 响应 `{"done": bool, "text_delta": "增量"}` 或 `{"done": true, "text": "完整回复"}`。
每个 `text_delta` 作为一个分段（回调 + segments）；`text` 为权威完整文本，覆盖增量拼接。

字段名均可改（真机联调对协议用）：

| 键 | 默认 | 说明 |
|---|---|---|
| `prompt_path` / `wait_path` | `/api/session/prompt` / `/api/session/wait` | 路径模板 |
| `prompt_field` / `history_field` | `prompt` / `history` | 请求字段名 |
| `session_field` | `session_id` | 响应取会话 id 的点路径 |
| `wait_session_field` | `session_id` | wait 请求里的会话 id 字段名 |
| `done_field` / `delta_field` / `text_field` | `done` / `text_delta` / `text` | wait 响应字段（点路径） |
| `poll_interval_ms` / `send_timeout_s` | `500` / `120` | 轮询间隔 / send 总超时 |

### 3.2 OpenAI 兼容（DeepSeek / 智谱 / qwen 等）

```json
{
  "kind": "openai_compat",
  "base_url": "https://api.deepseek.com",
  "api_key": "sk-xxxx",
  "model": "deepseek-chat",
  "extra": {}
}
```

`POST {base_url}/v1/chat/completions`，body `{"model", "messages", "stream": false}`，
取 `choices.0.message.content`。`base_url` 带不带 `/v1` 都行（自动去重）；
`extra.chat_path`（默认 `/v1/chat/completions`）与 `extra.response_path`
（默认 `choices.0.message.content`）可覆盖。

### 3.3 Anthropic Messages

```json
{
  "kind": "anthropic",
  "base_url": "https://api.anthropic.com",
  "api_key": "sk-ant-xxxx",
  "model": "claude-sonnet-4-5",
  "extra": {
    "max_tokens": "1024",
    "anthropic_version": "2023-06-01"
  }
}
```

`POST {base_url}/v1/messages`，头 `x-api-key` + `anthropic-version: 2023-06-01`，
body `{"model", "max_tokens", "messages"}`（history 里的 system 消息自动上提为顶层
`system` 字段），取 `content.0.text`。`extra.messages_path`、`extra.response_path` 可覆盖。

### 3.4 自定义（HTTP 模板 + JSON 点路径解析）

```json
{
  "kind": "custom",
  "base_url": "https://my-agent.example.com",
  "api_key": "tk-xxxx",
  "model": "my-model",
  "extra": {
    "method": "POST",
    "path": "/api/reply",
    "headers_json": "{\"Authorization\":\"Bearer {api_key}\",\"X-Model\":\"{model}\"}",
    "body_template": "{\"prompt\":\"{prompt}\",\"history\":{history_json}}",
    "response_path": "data.reply"
  }
}
```

- `url`（完整 URL）或 `path`（拼 `base_url`）；`method` 默认 `POST`（GET/HEAD 不发 body）。
- 占位符：`{prompt}` `{history_json}` `{model}` `{api_key}` `{note}`。
  **转义规则**：body 模板里 `{prompt}`/`{note}` 等自动做 JSON 字符串转义（模板写
  `"{prompt}"` 即可，引号安全）；URL 模板里自动百分号编码；header 模板里原文替换。
  名字以 `_json`/`_raw` 结尾（如 `{history_json}`）永远原文插入。未命中的 `{...}`
  原样保留（body 里的 JSON 花括号不受影响）。
- `headers_json`：JSON 对象，配置后**整体替换**默认请求头（不配则默认
  `Authorization: Bearer {api_key}` + `Accept` + `Content-Type`）。
- `response_path`：响应 JSON 点路径（如 `data.reply`、`data.0.text`）；**不配 = 整个
  响应体当纯文本**。找不到路径 → `Error`。

### 3.5 可选能力：看板 / 速记（DSH 与 Custom）

不配置即 `Unsupported`（入口自动隐藏）；配置后启用：

```json
{
  "extra": {
    "board_path": "/api/board",
    "board_method": "GET",
    "board_body_template": "",
    "note_path": "/api/note",
    "note_method": "POST",
    "note_body_template": "{\"note\":\"{note}\",\"model\":\"{model}\"}"
  }
}
```

`fetch_board()` 返回 `BoardResult.raw`（原始 JSON/文本，上层自行解读或用 `json_path` 提取）；
`push_note(text)` 发 `note_body_template`（默认 `{"note":"{note}"}`）。
`board_path`/`note_path` 同样支持 `{model}`/`{api_key}` 占位。

## 4. HTTP 客户端选型

**libcurl 优先**（sysroot 已带：`usr/include/aarch64-linux-gnu/curl/` + `libcurl.so/.a`），
编译期 `__has_include(<curl/curl.h>)` 自动选中；无 curl 头的构建自动退化为内置最小
HTTP/1.1 客户端（POSIX socket，POST JSON、Content-Length/分块、超时 10s 默认）。
`-DVADE_AGENT_NO_LIBCURL` 可强制内置实现。

## 5. 已知限制

1. **CMake 链接项**：curl 分支需要链接 `-lcurl`（本层未改 CMakeLists）。接线时给
   `vade_model` 或最终可执行文件加 `curl` 链接（sysroot 有 `libcurl.so`/`libcurl.a`，
   设备 rootfs 有 `libcurl.so.4` 运行时）。在链接加上之前，本层不被引用不报错
   （静态库按需拉取），一被引用就需要该项。
2. **流式**：OpenAI/Anthropic 均按 stream=false 一次性返回（契约允许的简化）；DSH 的
   "流式" 是 unary 轮询增量段，不是 SSE。Anthropic 的 SSE 事件流解析未做。
3. **内置 socket 客户端不支持 HTTPS**（无 TLS 库）——只在无 curl 头的构建才会走到；
   HTTPS 场景必须使用 libcurl 分支。
4. **DSH 真实协议待校准**：`session/prompt` + `session/wait` 的路径与字段名按通用
   unary 轮询假定实现，全部可配（见 3.1 字段表），真机联调按实际协议改配置即可。
5. 看板返回是原始负载（`BoardResult.raw`），未定义统一的任务结构——远端 kanban 格式
   未知，留由上层/配置解析。
6. 同步阻塞 + 单线程假定；无重试、无请求队列（上层负责调度与重试按钮）。
7. 迷你 JSON 解析只覆盖标准 JSON（含 `\uXXXX` 代理对），不做流式解析；
   响应体建议控制在几 MB 内（小屏场景足够）。
