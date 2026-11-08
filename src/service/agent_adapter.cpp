/*
 * Vade — pocket companion terminal
 * SPDX-License-Identifier: MIT
 */
#include "agent_adapter.h"

#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

// HTTP 传输选型：优先 libcurl（sysroot 自带头与库）；无 curl 头时退化为
// 内置最小 HTTP/1.1 客户端（POSIX socket，仅 http://，超时 10s 默认）。
// 可用 -DVADE_AGENT_NO_LIBCURL 强制走内置实现。
#if !defined(VADE_AGENT_NO_LIBCURL) && defined(__has_include)
#  if __has_include(<curl/curl.h>)
#    define VADE_AGENT_HAS_CURL 1
#  endif
#endif

#if defined(VADE_AGENT_HAS_CURL)
#  include <curl/curl.h>
#elif !defined(_WIN32)
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <sys/time.h>
#  include <unistd.h>
#endif

namespace service {

namespace {

// ============================================================================
// 小工具
// ============================================================================

constexpr int kDefaultHttpTimeoutMs = 10000; // 内置/默认 HTTP 超时 10s
constexpr int kDefaultPollIntervalMs = 500;
constexpr int kDefaultSendTimeoutS = 120;
constexpr int kDefaultMaxTokens = 1024;
constexpr size_t kErrorSnippetMax = 200;

std::string lower_ascii(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string trim(const std::string& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

bool ends_with(const std::string& s, const char* suffix) {
    const size_t n = std::char_traits<char>::length(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

bool is_var_name(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (char c : s) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
    }
    return true;
}

std::string snippet(const std::string& s) {
    std::string t = trim(s);
    if (t.size() > kErrorSnippetMax) t = t.substr(0, kErrorSnippetMax) + "...";
    return t;
}

// ============================================================================
// JSON 构造（请求体）
// ============================================================================

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

std::string json_quote(const std::string& s) {
    return "\"" + json_escape(s) + "\"";
}

std::string percent_encode(const std::string& s) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

// ============================================================================
// 迷你 JSON 解析（只读取值 + 点路径查询，无异常）
// ============================================================================

namespace json_mini {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;
};

struct Parser {
    const char* p = nullptr;
    const char* end = nullptr;
    std::string error;
};

void skip_ws(Parser& pr) {
    while (pr.p < pr.end &&
           (*pr.p == ' ' || *pr.p == '\t' || *pr.p == '\n' || *pr.p == '\r')) {
        ++pr.p;
    }
}

int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void append_utf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

bool parse_string(Parser& pr, std::string& out) {
    // 进入时 *pr.p == '"'
    ++pr.p;
    out.clear();
    while (pr.p < pr.end) {
        unsigned char c = static_cast<unsigned char>(*pr.p++);
        if (c == '"') return true;
        if (c != '\\') {
            out.push_back(static_cast<char>(c));
            continue;
        }
        if (pr.p >= pr.end) break;
        char e = *pr.p++;
        switch (e) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                unsigned cp = 0;
                for (int i = 0; i < 4; ++i) {
                    if (pr.p >= pr.end) {
                        pr.error = "bad \\u escape";
                        return false;
                    }
                    int hv = hex_val(*pr.p++);
                    if (hv < 0) {
                        pr.error = "bad \\u escape";
                        return false;
                    }
                    cp = cp * 16 + static_cast<unsigned>(hv);
                }
                // 代理对
                if (cp >= 0xD800 && cp <= 0xDBFF && pr.end - pr.p >= 6 &&
                    pr.p[0] == '\\' && pr.p[1] == 'u') {
                    unsigned lo = 0;
                    bool ok = true;
                    for (int i = 0; i < 4; ++i) {
                        int hv = hex_val(pr.p[2 + i]);
                        if (hv < 0) {
                            ok = false;
                            break;
                        }
                        lo = lo * 16 + static_cast<unsigned>(hv);
                    }
                    if (ok && lo >= 0xDC00 && lo <= 0xDFFF) {
                        pr.p += 6;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                }
                append_utf8(out, cp);
                break;
            }
            default:
                pr.error = "bad escape";
                return false;
        }
    }
    pr.error = "unterminated string";
    return false;
}

bool parse_value(Parser& pr, Value& out, int depth);

bool parse_object(Parser& pr, Value& out, int depth) {
    ++pr.p; // '{'
    out.type = Value::Type::Object;
    skip_ws(pr);
    if (pr.p < pr.end && *pr.p == '}') {
        ++pr.p;
        return true;
    }
    while (true) {
        skip_ws(pr);
        if (pr.p >= pr.end || *pr.p != '"') {
            pr.error = "expected object key";
            return false;
        }
        std::string key;
        if (!parse_string(pr, key)) return false;
        skip_ws(pr);
        if (pr.p >= pr.end || *pr.p != ':') {
            pr.error = "expected ':'";
            return false;
        }
        ++pr.p;
        Value val;
        if (!parse_value(pr, val, depth + 1)) return false;
        out.object.emplace_back(std::move(key), std::move(val));
        skip_ws(pr);
        if (pr.p < pr.end && *pr.p == ',') {
            ++pr.p;
            continue;
        }
        if (pr.p < pr.end && *pr.p == '}') {
            ++pr.p;
            return true;
        }
        pr.error = "expected ',' or '}'";
        return false;
    }
}

bool parse_array(Parser& pr, Value& out, int depth) {
    ++pr.p; // '['
    out.type = Value::Type::Array;
    skip_ws(pr);
    if (pr.p < pr.end && *pr.p == ']') {
        ++pr.p;
        return true;
    }
    while (true) {
        Value val;
        if (!parse_value(pr, val, depth + 1)) return false;
        out.array.push_back(std::move(val));
        skip_ws(pr);
        if (pr.p < pr.end && *pr.p == ',') {
            ++pr.p;
            continue;
        }
        if (pr.p < pr.end && *pr.p == ']') {
            ++pr.p;
            return true;
        }
        pr.error = "expected ',' or ']'";
        return false;
    }
}

bool parse_value(Parser& pr, Value& out, int depth) {
    if (depth > 64) {
        pr.error = "json too deep";
        return false;
    }
    skip_ws(pr);
    if (pr.p >= pr.end) {
        pr.error = "unexpected end";
        return false;
    }
    char c = *pr.p;
    if (c == '{') return parse_object(pr, out, depth);
    if (c == '[') return parse_array(pr, out, depth);
    if (c == '"') {
        out.type = Value::Type::String;
        return parse_string(pr, out.string);
    }
    if (c == 't') {
        if (pr.end - pr.p >= 4 && std::memcmp(pr.p, "true", 4) == 0) {
            pr.p += 4;
            out.type = Value::Type::Bool;
            out.boolean = true;
            return true;
        }
        pr.error = "bad literal";
        return false;
    }
    if (c == 'f') {
        if (pr.end - pr.p >= 5 && std::memcmp(pr.p, "false", 5) == 0) {
            pr.p += 5;
            out.type = Value::Type::Bool;
            out.boolean = false;
            return true;
        }
        pr.error = "bad literal";
        return false;
    }
    if (c == 'n') {
        if (pr.end - pr.p >= 4 && std::memcmp(pr.p, "null", 4) == 0) {
            pr.p += 4;
            out.type = Value::Type::Null;
            return true;
        }
        pr.error = "bad literal";
        return false;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        const char* start = pr.p;
        while (pr.p < pr.end &&
               (std::isdigit(static_cast<unsigned char>(*pr.p)) || *pr.p == '-' ||
                *pr.p == '+' || *pr.p == '.' || *pr.p == 'e' || *pr.p == 'E')) {
            ++pr.p;
        }
        std::string numstr(start, pr.p);
        char* endp = nullptr;
        out.type = Value::Type::Number;
        out.number = std::strtod(numstr.c_str(), &endp);
        if (endp == numstr.c_str()) {
            pr.error = "bad number";
            return false;
        }
        return true;
    }
    pr.error = "unexpected character";
    return false;
}

bool parse(const std::string& text, Value& out, std::string& error) {
    Parser pr;
    pr.p = text.data();
    pr.end = text.data() + text.size();
    if (!parse_value(pr, out, 0)) {
        error = pr.error;
        return false;
    }
    skip_ws(pr);
    if (pr.p != pr.end) {
        error = "trailing data";
        return false;
    }
    return true;
}

// 点路径：对象按 key，数组按数字下标（"choices.0.message.content"）。
const Value* get_path(const Value& root, const std::string& path) {
    const Value* cur = &root;
    if (path.empty()) return cur;
    size_t start = 0;
    while (true) {
        size_t dot = path.find('.', start);
        std::string seg =
            path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (cur->type == Value::Type::Object) {
            const Value* next = nullptr;
            for (const auto& kv : cur->object) {
                if (kv.first == seg) {
                    next = &kv.second;
                    break;
                }
            }
            if (!next) return nullptr;
            cur = next;
        } else if (cur->type == Value::Type::Array) {
            if (seg.empty()) return nullptr;
            size_t idx = 0;
            for (char ch : seg) {
                if (!std::isdigit(static_cast<unsigned char>(ch))) return nullptr;
                idx = idx * 10 + static_cast<size_t>(ch - '0');
                if (idx > 1000000) return nullptr;
            }
            if (idx >= cur->array.size()) return nullptr;
            cur = &cur->array[idx];
        } else {
            return nullptr;
        }
        if (dot == std::string::npos) return cur;
        start = dot + 1;
    }
}

std::string as_string(const Value& v) {
    switch (v.type) {
        case Value::Type::String:
            return v.string;
        case Value::Type::Bool:
            return v.boolean ? "true" : "false";
        case Value::Type::Number: {
            char buf[40];
            long long ll = static_cast<long long>(v.number);
            if (static_cast<double>(ll) == v.number) {
                std::snprintf(buf, sizeof(buf), "%lld", ll);
            } else {
                std::snprintf(buf, sizeof(buf), "%g", v.number);
            }
            return std::string(buf);
        }
        case Value::Type::Null:
        case Value::Type::Array:
        case Value::Type::Object:
            return std::string();
    }
    return std::string();
}

} // namespace json_mini

// ============================================================================
// 模板占位替换（自定义适配器）
//   {prompt} {note} {model} {api_key} 依模板所在位置转义：
//     body 模板   → JSON 字符串转义（模板写 "{prompt}"，引号安全）
//     URL 模板    → 百分号编码
//     header 模板 → 原文
//   名字以 _json / _raw 结尾的占位（如 {history_json}）永远原文插入。
//   未命中已知占位的 {...} 原样保留（body 模板里的 JSON 花括号不受影响）。
// ============================================================================

enum class SubMode { Raw, JsonEscape, UrlEncode };

std::string substitute(const std::string& tpl,
                       const std::map<std::string, std::string>& vars,
                       SubMode mode) {
    std::string out;
    out.reserve(tpl.size() + 32);
    size_t i = 0;
    while (i < tpl.size()) {
        char c = tpl[i];
        if (c == '{') {
            size_t close = tpl.find('}', i + 1);
            if (close != std::string::npos && close - i - 1 <= 64) {
                std::string name = tpl.substr(i + 1, close - i - 1);
                if (is_var_name(name)) {
                    auto it = vars.find(name);
                    if (it != vars.end()) {
                        const std::string& v = it->second;
                        if (mode == SubMode::Raw || ends_with(name, "_json") ||
                            ends_with(name, "_raw")) {
                            out += v;
                        } else if (mode == SubMode::JsonEscape) {
                            out += json_escape(v);
                        } else {
                            out += percent_encode(v);
                        }
                        i = close + 1;
                        continue;
                    }
                }
            }
        }
        out.push_back(c);
        ++i;
    }
    return out;
}

// ============================================================================
// 请求构造
// ============================================================================

std::string chat_messages_json(const std::vector<ChatMessage>& messages) {
    std::string out = "[";
    for (size_t i = 0; i < messages.size(); ++i) {
        if (i) out += ",";
        out += "{\"role\":" + json_quote(messages[i].role) +
               ",\"content\":" + json_quote(messages[i].content) + "}";
    }
    out += "]";
    return out;
}

std::string join_url(const std::string& base, const std::string& path) {
    if (path.rfind("http://", 0) == 0 || path.rfind("https://", 0) == 0) return path;
    if (base.empty()) return path;
    size_t end = base.find_last_not_of('/');
    std::string b = (end == std::string::npos) ? std::string() : base.substr(0, end + 1);
    if (!path.empty() && path[0] == '/') return b + path;
    return b + "/" + path;
}

std::string cfg_extra_get(const AgentConfig& config,
                          const std::string& key,
                          const std::string& fallback) {
    auto it = config.extra.find(key);
    if (it == config.extra.end() || it->second.empty()) return fallback;
    return it->second;
}

int cfg_extra_int(const AgentConfig& config, const std::string& key, int fallback) {
    auto it = config.extra.find(key);
    if (it == config.extra.end() || it->second.empty()) return fallback;
    char* endp = nullptr;
    long v = std::strtol(it->second.c_str(), &endp, 10);
    if (endp == it->second.c_str() || v <= 0 || v > 86400000) return fallback;
    return static_cast<int>(v);
}

enum class AuthStyle { Bearer, XApiKey, None };

struct HttpRequest {
    std::string method;
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    int timeout_ms = kDefaultHttpTimeoutMs;
    bool insecure_tls = false;
};

struct HttpResponse {
    bool ok = false;   // 传输层成功
    long status = 0;   // HTTP 状态码
    std::string body;
    std::string error; // 传输层错误描述
};

bool has_header_ci(const std::vector<std::pair<std::string, std::string>>& headers,
                   const std::string& name) {
    const std::string want = lower_ascii(name);
    for (const auto& h : headers) {
        if (lower_ascii(h.first) == want) return true;
    }
    return false;
}

HttpRequest make_request(const AgentConfig& config,
                         const std::string& method,
                         const std::string& url,
                         const std::string& body,
                         AuthStyle auth) {
    HttpRequest req;
    req.method = method.empty() ? "POST" : method;
    req.url = url;
    req.body = body;
    req.timeout_ms = cfg_extra_int(config, "http_timeout_ms", kDefaultHttpTimeoutMs);
    const std::string insecure = cfg_extra_get(config, "insecure_tls", "");
    req.insecure_tls = (insecure == "1" || insecure == "true");
    req.headers.emplace_back("Accept", "application/json, text/plain, */*");
    if (!body.empty()) req.headers.emplace_back("Content-Type", "application/json");
    if (!config.api_key.empty()) {
        if (auth == AuthStyle::Bearer) {
            req.headers.emplace_back("Authorization", "Bearer " + config.api_key);
        } else if (auth == AuthStyle::XApiKey) {
            req.headers.emplace_back("x-api-key", config.api_key);
        }
    }
    return req;
}

// headers_json：{"Authorization":"Bearer {api_key}", ...}，配置后整体替换默认头。
bool apply_headers_json(const AgentConfig& config,
                        HttpRequest& request,
                        const std::map<std::string, std::string>& vars,
                        std::string& error) {
    const std::string headers_json = cfg_extra_get(config, "headers_json", "");
    if (headers_json.empty()) return true;
    json_mini::Value hv;
    std::string perr;
    if (!json_mini::parse(headers_json, hv, perr) ||
        hv.type != json_mini::Value::Type::Object) {
        error = "headers_json 解析失败: " + perr;
        return false;
    }
    request.headers.clear();
    for (const auto& kv : hv.object) {
        request.headers.emplace_back(
            kv.first, substitute(json_mini::as_string(kv.second), vars, SubMode::Raw));
    }
    if (!request.body.empty() && !has_header_ci(request.headers, "content-type")) {
        request.headers.emplace_back("Content-Type", "application/json");
    }
    return true;
}

std::string http_error(const char* what, const HttpResponse& resp) {
    return std::string(what) + ": HTTP " + std::to_string(resp.status) + " " +
           snippet(resp.body);
}

// ============================================================================
// HTTP 传输层
// ============================================================================

#if defined(VADE_AGENT_HAS_CURL)

size_t curl_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    const size_t total = size * nmemb;
    static_cast<std::string*>(userdata)->append(ptr, total);
    return total;
}

struct CurlGlobal {
    CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobal() { curl_global_cleanup(); }
};

HttpResponse http_request(const HttpRequest& req) {
    static CurlGlobal curl_global; // 每进程一次
    (void)curl_global;

    HttpResponse out;
    CURL* curl = curl_easy_init();
    if (!curl) {
        out.error = "curl_easy_init failed";
        return out;
    }
    std::string body;
    struct curl_slist* headers = nullptr;
    for (const auto& h : req.headers) {
        const std::string line = h.first + ": " + h.second;
        headers = curl_slist_append(headers, line.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_URL, req.url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, req.method.c_str());
    if (!req.body.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(req.body.size()));
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(req.timeout_ms));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(req.timeout_ms));
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L); // 工作线程安全
    if (req.insecure_tls) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    const CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        out.error = std::string("curl: ") + curl_easy_strerror(rc);
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out.status);
        out.body = std::move(body);
        out.ok = true;
    }
    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return out;
}

#elif defined(_WIN32)

// Windows 构建且无 libcurl：无可用传输（交叉目标为 Linux，仅保底）。
HttpResponse http_request(const HttpRequest&) {
    HttpResponse out;
    out.error = "no HTTP transport in this build (need libcurl)";
    return out;
}

#else

// ---- 最小 HTTP/1.1 客户端（POSIX socket）：POST JSON / Content-Length / 分块 ----

#ifndef MSG_NOSIGNAL
#  define MSG_NOSIGNAL 0
#endif

int remaining_ms(const std::chrono::steady_clock::time_point& deadline) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) return 0;
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
}

// 1=收全且合法 0=未收完 -1=格式错误
int decode_chunked(const std::string& in, std::string& out) {
    out.clear();
    size_t pos = 0;
    while (true) {
        size_t eol = in.find("\r\n", pos);
        if (eol == std::string::npos) return 0;
        std::string size_line = in.substr(pos, eol - pos);
        const size_t semi = size_line.find(';');
        if (semi != std::string::npos) size_line.resize(semi);
        size_line = trim(size_line);
        if (size_line.empty()) return -1;
        unsigned long long clen = 0;
        for (char ch : size_line) {
            int hv = json_mini::hex_val(ch);
            if (hv < 0) return -1;
            clen = clen * 16 + static_cast<unsigned long long>(hv);
            if (clen > 64u * 1024u * 1024u) return -1;
        }
        pos = eol + 2;
        if (clen == 0) return 1; // 结束块（trailer 忽略）
        if (in.size() < pos + clen + 2) return 0;
        out.append(in, pos, clen);
        pos += clen;
        if (in.compare(pos, 2, "\r\n") != 0) return -1;
        pos += 2;
    }
}

HttpResponse http_request(const HttpRequest& req) {
    HttpResponse out;
    if (req.url.rfind("http://", 0) != 0) {
        out.error = "内置 HTTP 客户端仅支持 http://（HTTPS 请用带 libcurl 的构建）";
        return out;
    }
    const std::string rest = req.url.substr(7);
    std::string hostport;
    std::string path = "/";
    const size_t slash = rest.find('/');
    if (slash == std::string::npos) {
        hostport = rest;
    } else {
        hostport = rest.substr(0, slash);
        path = rest.substr(slash);
    }
    std::string host = hostport;
    std::string port = "80";
    const size_t colon = hostport.rfind(':');
    if (colon != std::string::npos) {
        host = hostport.substr(0, colon);
        port = hostport.substr(colon + 1);
    }
    if (host.empty()) {
        out.error = "bad url: empty host";
        return out;
    }

    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    const int gai = ::getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
    if (gai != 0 || !res) {
        out.error = std::string("dns: ") + ::gai_strerror(gai);
        return out;
    }

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(req.timeout_ms);
    int fd = -1;
    for (struct addrinfo* ai = res; ai; ai = ai->ai_next) {
        fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        const int flags = ::fcntl(fd, F_GETFL, 0);
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int rc = ::connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (rc != 0 && errno != EINPROGRESS) {
            ::close(fd);
            fd = -1;
            continue;
        }
        if (rc != 0) {
            struct pollfd pfd;
            pfd.fd = fd;
            pfd.events = POLLOUT;
            pfd.revents = 0;
            const int remain = remaining_ms(deadline);
            if (remain <= 0 || ::poll(&pfd, 1, remain) <= 0) {
                ::close(fd);
                fd = -1;
                continue;
            }
            int err = 0;
            socklen_t len = sizeof(err);
            ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
            if (err != 0) {
                ::close(fd);
                fd = -1;
                continue;
            }
        }
        ::fcntl(fd, F_SETFL, flags);
        break;
    }
    ::freeaddrinfo(res);
    if (fd < 0) {
        out.error = "connect failed or timed out";
        return out;
    }

    struct timeval tv;
    tv.tv_sec = req.timeout_ms / 1000;
    tv.tv_usec = (req.timeout_ms % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    std::string req_text;
    req_text += req.method.empty() ? "POST" : req.method;
    req_text += " ";
    req_text += path;
    req_text += " HTTP/1.1\r\n";
    req_text += "Host: " + hostport + "\r\n";
    req_text += "Connection: close\r\n";
    for (const auto& h : req.headers) {
        req_text += h.first + ": " + h.second + "\r\n";
    }
    if (!req.body.empty()) {
        req_text += "Content-Length: " + std::to_string(req.body.size()) + "\r\n";
    }
    req_text += "\r\n";
    req_text += req.body;

    size_t sent = 0;
    while (sent < req_text.size()) {
        const ssize_t n = ::send(fd, req_text.data() + sent, req_text.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            ::close(fd);
            out.error = "send failed or timed out";
            return out;
        }
        sent += static_cast<size_t>(n);
    }

    std::string raw;
    size_t header_end = std::string::npos;
    long content_length = -1;
    bool chunked = false;
    char buf[2048];
    while (true) {
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            raw.append(buf, static_cast<size_t>(n));
            if (header_end == std::string::npos) {
                header_end = raw.find("\r\n\r\n");
                if (header_end != std::string::npos) {
                    const std::string header_block = raw.substr(0, header_end);
                    size_t line_start = header_block.find("\r\n");
                    size_t pos = (line_start == std::string::npos) ? header_block.size()
                                                                  : line_start + 2;
                    while (pos < header_block.size()) {
                        size_t eol = header_block.find("\r\n", pos);
                        if (eol == std::string::npos) eol = header_block.size();
                        const std::string line = header_block.substr(pos, eol - pos);
                        pos = eol + 2;
                        const size_t colon2 = line.find(':');
                        if (colon2 == std::string::npos) continue;
                        const std::string name = lower_ascii(trim(line.substr(0, colon2)));
                        const std::string value = trim(line.substr(colon2 + 1));
                        if (name == "content-length") {
                            content_length = std::strtol(value.c_str(), nullptr, 10);
                        } else if (name == "transfer-encoding" &&
                                   lower_ascii(value).find("chunked") != std::string::npos) {
                            chunked = true;
                        }
                    }
                }
            }
        } else {
            if (n < 0 && errno == EINTR) continue;
            // 连接关闭/超时：帧长已收全则视为完成
            bool done = false;
            if (header_end != std::string::npos) {
                const size_t body_have = raw.size() - (header_end + 4);
                if (chunked) {
                    std::string tmp;
                    done = decode_chunked(raw.substr(header_end + 4), tmp) == 1;
                } else if (content_length >= 0) {
                    done = body_have >= static_cast<size_t>(content_length);
                }
            }
            if (!done) {
                ::close(fd);
                out.error = (n < 0) ? "recv failed or timed out" : "connection closed early";
                return out;
            }
            break;
        }
        if (header_end != std::string::npos) {
            const size_t body_have = raw.size() - (header_end + 4);
            if (chunked) {
                std::string tmp;
                if (decode_chunked(raw.substr(header_end + 4), tmp) == 1) break;
            } else if (content_length >= 0 && body_have >= static_cast<size_t>(content_length)) {
                break;
            }
        }
        if (remaining_ms(deadline) == 0) {
            ::close(fd);
            out.error = "recv timed out";
            return out;
        }
    }
    ::close(fd);

    if (header_end == std::string::npos) {
        out.error = "malformed HTTP response";
        return out;
    }
    const size_t sp = raw.find(' ');
    if (sp == std::string::npos) {
        out.error = "bad status line";
        return out;
    }
    out.status = std::strtol(raw.c_str() + sp + 1, nullptr, 10);
    std::string body = raw.substr(header_end + 4);
    if (chunked) {
        std::string decoded;
        if (decode_chunked(body, decoded) != 1) {
            out.error = "bad chunked body";
            return out;
        }
        body = std::move(decoded);
    } else if (content_length >= 0 && body.size() > static_cast<size_t>(content_length)) {
        body.resize(static_cast<size_t>(content_length));
    }
    out.body = std::move(body);
    out.ok = true;
    return out;
}

#endif

} // namespace

// ============================================================================
// AgentAdapter
// ============================================================================

AgentAdapter::AgentAdapter(const AgentConfig& config) : config_(config) {}

void AgentAdapter::set_config(const AgentConfig& config) {
    config_ = config;
}

const AgentConfig& AgentAdapter::config() const {
    return config_;
}

std::string AgentAdapter::extra_get(const std::string& key, const std::string& fallback) const {
    return cfg_extra_get(config_, key, fallback);
}

int AgentAdapter::extra_int(const std::string& key, int fallback) const {
    return cfg_extra_int(config_, key, fallback);
}

bool AgentAdapter::supports(AgentAbility ability) const {
    switch (ability) {
        case AgentAbility::Send:
            return true;
        case AgentAbility::FetchBoard:
            return (config_.kind == AgentKind::Dsh || config_.kind == AgentKind::Custom) &&
                   !extra_get("board_path", "").empty();
        case AgentAbility::PushNote:
            return (config_.kind == AgentKind::Dsh || config_.kind == AgentKind::Custom) &&
                   !extra_get("note_path", "").empty();
    }
    return false;
}

SendResult AgentAdapter::send(const std::string& prompt,
                              const std::vector<ChatMessage>& history,
                              const SegmentCallback& on_segment) {
    switch (config_.kind) {
        case AgentKind::Dsh:
            return dsh_send(prompt, history, on_segment);
        case AgentKind::OpenAiCompat:
            return openai_send(prompt, history, on_segment);
        case AgentKind::Anthropic:
            return anthropic_send(prompt, history, on_segment);
        case AgentKind::Custom:
            return custom_send(prompt, history, on_segment);
    }
    SendResult r;
    r.error = "unknown agent kind";
    return r;
}

// ---- DSH：session/prompt 提交 + session/wait 轮询（unary，分段=增量段） ----

SendResult AgentAdapter::dsh_send(const std::string& prompt,
                                  const std::vector<ChatMessage>& history,
                                  const SegmentCallback& on_segment) {
    SendResult r;
    const std::string prompt_path = extra_get("prompt_path", "/api/session/prompt");
    const std::string wait_path = extra_get("wait_path", "/api/session/wait");
    const std::string prompt_field = extra_get("prompt_field", "prompt");
    const std::string history_field = extra_get("history_field", "history");
    const std::string session_field = extra_get("session_field", "session_id");
    const std::string wait_session_field = extra_get("wait_session_field", "session_id");
    const std::string done_field = extra_get("done_field", "done");
    const std::string delta_field = extra_get("delta_field", "text_delta");
    const std::string text_field = extra_get("text_field", "text");
    const int poll_ms = extra_int("poll_interval_ms", kDefaultPollIntervalMs);
    const int send_timeout_s = extra_int("send_timeout_s", kDefaultSendTimeoutS);

    // 1) 提交 prompt，取 session_id
    std::string body = "{" + json_quote(prompt_field) + ":" + json_quote(prompt) + "," +
                       json_quote(history_field) + ":" + chat_messages_json(history);
    if (!config_.model.empty()) {
        body += ",\"model\":" + json_quote(config_.model);
    }
    body += "}";

    const HttpResponse resp = http_request(make_request(
        config_, "POST", join_url(config_.base_url, prompt_path), body, AuthStyle::Bearer));
    if (!resp.ok) {
        r.status = CapabilityStatus::Error;
        r.error = "dsh prompt: " + resp.error;
        return r;
    }
    if (resp.status < 200 || resp.status >= 300) {
        r.status = CapabilityStatus::Error;
        r.error = http_error("dsh prompt", resp);
        return r;
    }
    bool got = false;
    const std::string session_id = json_path(resp.body, session_field, &got);
    if (!got || session_id.empty()) {
        r.status = CapabilityStatus::Error;
        r.error = "dsh prompt: 响应缺少 " + session_field;
        return r;
    }

    // 2) 轮询 wait：增量段逐段回调，done 后收口
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(send_timeout_s);
    const std::string wait_body =
        "{" + json_quote(wait_session_field) + ":" + json_quote(session_id) + "}";
    std::string assembled;
    bool finished = false;
    while (!finished) {
        if (std::chrono::steady_clock::now() >= deadline) {
            r.status = CapabilityStatus::Error;
            r.error = "dsh wait: 超时（send_timeout_s=" + std::to_string(send_timeout_s) + "）";
            r.text = assembled;
            return r;
        }
        const HttpResponse w = http_request(make_request(
            config_, "POST", join_url(config_.base_url, wait_path), wait_body, AuthStyle::Bearer));
        if (!w.ok) {
            r.status = CapabilityStatus::Error;
            r.error = "dsh wait: " + w.error;
            r.text = assembled;
            return r;
        }
        if (w.status < 200 || w.status >= 300) {
            r.status = CapabilityStatus::Error;
            r.error = http_error("dsh wait", w);
            r.text = assembled;
            return r;
        }

        bool ok_flag = false;
        const std::string done_s = json_path(w.body, done_field, &ok_flag);
        finished = ok_flag && (done_s == "true" || done_s == "1");

        bool ok_delta = false;
        const std::string delta = json_path(w.body, delta_field, &ok_delta);
        if (ok_delta && !delta.empty()) {
            r.segments.push_back(delta);
            assembled += delta;
            if (on_segment) on_segment(delta);
        }

        // 完整文本字段为权威值：覆盖增量拼接
        bool ok_text = false;
        const std::string full = json_path(w.body, text_field, &ok_text);
        if (ok_text && !full.empty()) {
            assembled = full;
        }

        if (!finished) {
            std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
        }
    }
    if (r.segments.empty() && !assembled.empty()) {
        r.segments.push_back(assembled);
    }
    r.text = assembled;
    r.done = true;
    r.status = CapabilityStatus::Ok;
    return r;
}

// ---- OpenAI 兼容：POST /v1/chat/completions（stream=false） ----

SendResult AgentAdapter::openai_send(const std::string& prompt,
                                     const std::vector<ChatMessage>& history,
                                     const SegmentCallback& on_segment) {
    (void)on_segment;
    SendResult r;
    const std::string chat_path = extra_get("chat_path", "/v1/chat/completions");
    std::vector<ChatMessage> messages = history;
    messages.push_back(ChatMessage{"user", prompt});
    const std::string body = "{\"model\":" + json_quote(config_.model) +
                             ",\"messages\":" + chat_messages_json(messages) +
                             ",\"stream\":false}";

    // 允许 base_url 带 /v1（OpenAI SDK 风格），避免拼出 /v1/v1
    std::string base = config_.base_url;
    if (ends_with(base, "/v1") && chat_path.rfind("/v1", 0) == 0) {
        base = base.substr(0, base.size() - 3);
    }
    const HttpResponse resp = http_request(make_request(
        config_, "POST", join_url(base, chat_path), body, AuthStyle::Bearer));
    if (!resp.ok) {
        r.status = CapabilityStatus::Error;
        r.error = "openai: " + resp.error;
        return r;
    }
    if (resp.status < 200 || resp.status >= 300) {
        r.status = CapabilityStatus::Error;
        r.error = http_error("openai", resp);
        bool ok = false;
        const std::string msg = json_path(resp.body, "error.message", &ok);
        if (ok && !msg.empty()) r.error += " (" + msg + ")";
        return r;
    }
    bool ok = false;
    const std::string content =
        json_path(resp.body, extra_get("response_path", "choices.0.message.content"), &ok);
    if (!ok) {
        r.status = CapabilityStatus::Error;
        r.error = "openai: 响应缺少 choices[0].message.content";
        return r;
    }
    r.segments.push_back(content);
    r.text = content;
    r.done = true;
    r.status = CapabilityStatus::Ok;
    return r;
}

// ---- Anthropic Messages：POST /v1/messages，取 content[0].text ----

SendResult AgentAdapter::anthropic_send(const std::string& prompt,
                                        const std::vector<ChatMessage>& history,
                                        const SegmentCallback& on_segment) {
    (void)on_segment;
    SendResult r;
    const std::string messages_path = extra_get("messages_path", "/v1/messages");
    // system 消息上提为顶层 system 字段（Messages API 不收 system 角色）
    std::string system_text;
    std::vector<ChatMessage> messages;
    for (const auto& m : history) {
        if (m.role == "system") {
            if (!system_text.empty()) system_text += "\n";
            system_text += m.content;
        } else {
            messages.push_back(m);
        }
    }
    messages.push_back(ChatMessage{"user", prompt});
    std::string body = "{\"model\":" + json_quote(config_.model) +
                       ",\"max_tokens\":" +
                       std::to_string(extra_int("max_tokens", kDefaultMaxTokens)) +
                       ",\"messages\":" + chat_messages_json(messages);
    if (!system_text.empty()) {
        body += ",\"system\":" + json_quote(system_text);
    }
    body += "}";

    HttpRequest req = make_request(
        config_, "POST", join_url(config_.base_url, messages_path), body, AuthStyle::XApiKey);
    req.headers.emplace_back("anthropic-version",
                             extra_get("anthropic_version", "2023-06-01"));
    const HttpResponse resp = http_request(req);
    if (!resp.ok) {
        r.status = CapabilityStatus::Error;
        r.error = "anthropic: " + resp.error;
        return r;
    }
    if (resp.status < 200 || resp.status >= 300) {
        r.status = CapabilityStatus::Error;
        r.error = http_error("anthropic", resp);
        bool ok = false;
        const std::string msg = json_path(resp.body, "error.message", &ok);
        if (ok && !msg.empty()) r.error += " (" + msg + ")";
        return r;
    }
    bool ok = false;
    const std::string content =
        json_path(resp.body, extra_get("response_path", "content.0.text"), &ok);
    if (!ok) {
        r.status = CapabilityStatus::Error;
        r.error = "anthropic: 响应缺少 content[0].text";
        return r;
    }
    r.segments.push_back(content);
    r.text = content;
    r.done = true;
    r.status = CapabilityStatus::Ok;
    return r;
}

// ---- 自定义：HTTP 模板 + JSON 点路径响应解析 ----

SendResult AgentAdapter::custom_send(const std::string& prompt,
                                     const std::vector<ChatMessage>& history,
                                     const SegmentCallback& on_segment) {
    (void)on_segment;
    SendResult r;
    const std::string method = extra_get("method", "POST");
    std::string url_tpl = extra_get("url", "");
    if (url_tpl.empty()) url_tpl = extra_get("path", "");
    if (url_tpl.empty()) {
        r.status = CapabilityStatus::Error;
        r.error = "custom: extra.url 或 extra.path 未配置";
        return r;
    }

    std::map<std::string, std::string> vars;
    vars["prompt"] = prompt;
    vars["history_json"] = chat_messages_json(history);
    vars["model"] = config_.model;
    vars["api_key"] = config_.api_key;

    std::string body;
    const bool want_body = (method != "GET" && method != "HEAD");
    if (want_body) {
        std::string body_tpl =
            extra_get("body_template", "{\"prompt\":\"{prompt}\",\"history\":{history_json}}");
        body = substitute(body_tpl, vars, SubMode::JsonEscape);
    }

    HttpRequest req = make_request(config_, method,
                                   join_url(config_.base_url, substitute(url_tpl, vars, SubMode::UrlEncode)),
                                   body, AuthStyle::Bearer);
    if (!apply_headers_json(config_, req, vars, r.error)) {
        r.status = CapabilityStatus::Error;
        return r;
    }

    const HttpResponse resp = http_request(req);
    if (!resp.ok) {
        r.status = CapabilityStatus::Error;
        r.error = "custom: " + resp.error;
        return r;
    }
    if (resp.status < 200 || resp.status >= 300) {
        r.status = CapabilityStatus::Error;
        r.error = http_error("custom", resp);
        return r;
    }
    const std::string response_path = extra_get("response_path", "");
    std::string content;
    if (response_path.empty()) {
        content = resp.body; // 纯文本响应
    } else {
        bool ok = false;
        content = json_path(resp.body, response_path, &ok);
        if (!ok) {
            r.status = CapabilityStatus::Error;
            r.error = "custom: 响应中找不到 " + response_path;
            return r;
        }
    }
    r.segments.push_back(content);
    r.text = content;
    r.done = true;
    r.status = CapabilityStatus::Ok;
    return r;
}

// ---- 可选能力：看板 / 速记 ----

BoardResult AgentAdapter::fetch_board() {
    BoardResult r;
    if (config_.kind != AgentKind::Dsh && config_.kind != AgentKind::Custom) {
        r.status = CapabilityStatus::Unsupported;
        return r;
    }
    const std::string path = extra_get("board_path", "");
    if (path.empty()) {
        r.status = CapabilityStatus::Unsupported;
        return r;
    }

    std::map<std::string, std::string> vars;
    vars["model"] = config_.model;
    vars["api_key"] = config_.api_key;

    const std::string method = extra_get("board_method", "GET");
    std::string body;
    const std::string body_tpl = extra_get("board_body_template", "");
    if (!body_tpl.empty()) {
        body = substitute(body_tpl, vars, SubMode::JsonEscape);
    }

    HttpRequest req = make_request(
        config_,
        method,
        join_url(config_.base_url, substitute(path, vars, SubMode::UrlEncode)),
        body,
        AuthStyle::Bearer);
    if (!apply_headers_json(config_, req, vars, r.error)) {
        r.status = CapabilityStatus::Error;
        return r;
    }
    const HttpResponse resp = http_request(req);
    if (!resp.ok) {
        r.status = CapabilityStatus::Error;
        r.error = "board: " + resp.error;
        return r;
    }
    if (resp.status < 200 || resp.status >= 300) {
        r.status = CapabilityStatus::Error;
        r.error = http_error("board", resp);
        return r;
    }
    r.raw = resp.body;
    r.status = CapabilityStatus::Ok;
    return r;
}

NoteResult AgentAdapter::push_note(const std::string& note) {
    NoteResult r;
    if (config_.kind != AgentKind::Dsh && config_.kind != AgentKind::Custom) {
        r.status = CapabilityStatus::Unsupported;
        return r;
    }
    const std::string path = extra_get("note_path", "");
    if (path.empty()) {
        r.status = CapabilityStatus::Unsupported;
        return r;
    }

    std::map<std::string, std::string> vars;
    vars["note"] = note;
    vars["model"] = config_.model;
    vars["api_key"] = config_.api_key;

    const std::string method = extra_get("note_method", "POST");
    const std::string body_tpl = extra_get("note_body_template", "{\"note\":\"{note}\"}");
    const std::string body = substitute(body_tpl, vars, SubMode::JsonEscape);

    HttpRequest req = make_request(
        config_,
        method,
        join_url(config_.base_url, substitute(path, vars, SubMode::UrlEncode)),
        body,
        AuthStyle::Bearer);
    if (!apply_headers_json(config_, req, vars, r.error)) {
        r.status = CapabilityStatus::Error;
        return r;
    }
    const HttpResponse resp = http_request(req);
    if (!resp.ok) {
        r.status = CapabilityStatus::Error;
        r.error = "note: " + resp.error;
        return r;
    }
    if (resp.status < 200 || resp.status >= 300) {
        r.status = CapabilityStatus::Error;
        r.error = http_error("note", resp);
        return r;
    }
    r.status = CapabilityStatus::Ok;
    return r;
}

// ============================================================================
// 公共辅助
// ============================================================================

std::string json_path(const std::string& json, const std::string& path, bool* ok) {
    if (ok) *ok = false;
    json_mini::Value root;
    std::string error;
    if (!json_mini::parse(json, root, error)) return std::string();
    const json_mini::Value* v = json_mini::get_path(root, path);
    if (!v) return std::string();
    if (ok) *ok = true;
    return json_mini::as_string(*v);
}

const char* to_string(CapabilityStatus status) {
    switch (status) {
        case CapabilityStatus::Ok:
            return "ok";
        case CapabilityStatus::Unsupported:
            return "unsupported";
        case CapabilityStatus::Error:
            return "error";
    }
    return "error";
}

const char* to_string(AgentKind kind) {
    switch (kind) {
        case AgentKind::Dsh:
            return "dsh";
        case AgentKind::OpenAiCompat:
            return "openai_compat";
        case AgentKind::Anthropic:
            return "anthropic";
        case AgentKind::Custom:
            return "custom";
    }
    return "unknown";
}

} // namespace service
