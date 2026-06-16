#include "pch.h"

#include "llm_client.h"
#include "encoding.h"
#include "httplib.h"
#include "json.hpp"

namespace agent {
using json = nlohmann::json;

LLMClient::LLMClient(const std::string& base_url, const std::string& model)
    : base_url_(base_url), model_(model) {}

// ---------------------------------------------------------------------------
// URL parsing helper
// ---------------------------------------------------------------------------

LLMClient::UrlParts LLMClient::parse_url() const {
    UrlParts parts;
    size_t scheme_end = base_url_.find("://");
    if (scheme_end == std::string::npos) return parts;

    parts.is_ssl = (base_url_.rfind("http:", 0) != 0);
    std::string host_port = base_url_.substr(scheme_end + 3);
    size_t path_start = host_port.find('/');
    std::string host = (path_start != std::string::npos) ? host_port.substr(0, path_start) : host_port;

    size_t port_pos = host.find(':');
    if (port_pos != std::string::npos) {
        parts.host = host.substr(0, port_pos);
        parts.port = std::stoi(host.substr(port_pos + 1));
    } else {
        parts.host = host;
        parts.port = parts.is_ssl ? 443 : 80;
    }
    return parts;
}

// ---------------------------------------------------------------------------
// Non-streaming POST (unchanged)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Non-streaming POST - template to handle both Client and SSLClient
// ---------------------------------------------------------------------------

template<typename ClientType>
std::string post_json_impl(const std::string& path, const std::string& json_body,
                           const LLMClient::UrlParts& url) {
    ClientType client(url.host, url.port);
    if (!client.is_valid()) return "{}";

    client.set_read_timeout(30, 0);
    client.set_write_timeout(30, 0);

    auto res = client.Post(path, json_body, "application/json");
    if (res && res->status == 200) {
        return res->body;
    }
    return "{}";
}

std::string LLMClient::post_json(const std::string& path, const std::string& json_body) {
    auto url = parse_url();
    if (url.host.empty()) return "{}";

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    if (url.is_ssl)
        return post_json_impl<httplib::SSLClient>(path, json_body, url);
    else
        return post_json_impl<httplib::Client>(path, json_body, url);
#else
    if (url.is_ssl) return "{}";
    return post_json_impl<httplib::Client>(path, json_body, url);
#endif
}

// ---------------------------------------------------------------------------
// Shared JSON builder using nlohmann::json
// ---------------------------------------------------------------------------

std::string LLMClient::build_chat_json(
    const std::vector<ChatMessage>& messages,
    const std::vector<ToolDefinition>& tools,
    double temperature,
    int max_tokens,
    bool stream) {

    json req;
    req["model"] = model_;
    req["temperature"] = temperature;
    req["max_tokens"] = max_tokens;
    if (stream) req["stream"] = true;

    // Messages array
    json messages_arr = json::array();
    for (const auto& m : messages) {
        json msg;
        msg["role"] = m.role;
        msg["content"] = m.content;
        if (!m.name.empty()) msg["name"] = m.name;
        messages_arr.push_back(msg);
    }
    req["messages"] = messages_arr;

    // Tools array (if any)
    if (!tools.empty()) {
        json tools_arr = json::array();
        for (const auto& t : tools) {
            json tool_def;
            tool_def["type"] = "function";
            json func;
            func["name"] = t.name;
            func["description"] = t.description;
            // parameters_schema is already a JSON string, parse it
            try {
                func["parameters"] = json::parse(t.parameters_schema);
            } catch (...) {
                func["parameters"] = json::object();
            }
            tool_def["function"] = func;
            tools_arr.push_back(tool_def);
        }
        req["tools"] = tools_arr;
        req["tool_choice"] = "auto";
    }

    return req.dump();
}

// ---------------------------------------------------------------------------
// SSE data-line parser using nlohmann::json
// ---------------------------------------------------------------------------

void LLMClient::parse_sse_data(const std::string& data_line, ChatResponse& resp) {
    if (data_line == "[DONE]") return;

    try {
        json data = json::parse(data_line);

        // Extract usage from the final SSE chunk (before [DONE])
        if (data.contains("usage")) {
            auto& u = data["usage"];
            resp.prompt_tokens      = u.value("prompt_tokens", 0u);
            resp.completion_tokens  = u.value("completion_tokens", 0u);
        }

        // Extract choices[0].delta.content
        if (data.contains("choices") && !data["choices"].empty()) {
            auto& choice = data["choices"][0];
            if (choice.contains("delta")) {
                auto& delta = choice["delta"];

                // Reasoning/thinking token
                if (delta.contains("reasoning_content") && !delta["reasoning_content"].is_null()) {
                    std::string reasoning = delta["reasoning_content"].get<std::string>();
                    resp.reasoning_content += reasoning;
                }

                // Content token
                if (delta.contains("content") && !delta["content"].is_null()) {
                    std::string content = delta["content"].get<std::string>();
                    resp.content += content;
                }

                // Tool calls in the stream chunk — merge into existing calls by index.
                // OpenAI streaming sends tool_call chunks split across SSE events:
                //   first chunk  → {index, id, function:{name}}
                //   later chunks → {index, function:{arguments}} (id may be absent)
                if (delta.contains("tool_calls")) {
                    resp.has_tool_calls = true;
                    for (auto& tc : delta["tool_calls"]) {
                        int index = 0;
                        if (tc.contains("index") && !tc["index"].is_null())
                            index = tc["index"].get<int>();

                        // Ensure we have enough entries for this index.
                        while (static_cast<int>(resp.tool_calls.size()) <= index) {
                            resp.tool_calls.emplace_back();
                        }
                        auto& call = resp.tool_calls[index];

                        if (tc.contains("id") && !tc["id"].is_null() && call.id.empty())
                            call.id = tc["id"].get<std::string>();

                        if (tc.contains("function")) {
                            auto& func = tc["function"];
                            if (func.contains("name") && call.name.empty())
                                call.name = func["name"].get<std::string>();
                            if (func.contains("arguments") && !func["arguments"].is_null())
                                call.arguments += func["arguments"].get<std::string>();
                        }
                    }
                }
            }

            // Non-streaming response: choices[0].message.content / tool_calls
            if (choice.contains("message")) {
                auto& msg = choice["message"];
                if (msg.contains("content") && !msg["content"].is_null()) {
                    resp.content = msg["content"].get<std::string>();
                }
                if (msg.contains("tool_calls")) {
                    resp.has_tool_calls = true;
                    for (auto& tc : msg["tool_calls"]) {
                        ChatResponse::ToolCall call;
                        if (tc.contains("id") && !tc["id"].is_null())
                            call.id = tc["id"].get<std::string>();
                        if (tc.contains("function")) {
                            auto& func = tc["function"];
                            if (func.contains("name"))
                                call.name = func["name"].get<std::string>();
                            if (func.contains("arguments") && !func["arguments"].is_null())
                                call.arguments = func["arguments"].get<std::string>();
                        }
                        resp.tool_calls.push_back(call);
                    }
                }
            }
        }
    } catch (const json::parse_error&) {
        // Ignore malformed SSE lines
    }
}

// ---------------------------------------------------------------------------
// Non-streaming chat
// ---------------------------------------------------------------------------

ChatResponse LLMClient::chat(
    const std::vector<ChatMessage>& messages,
    const std::vector<ToolDefinition>& tools,
    double temperature,
    int max_tokens) {

    std::string json_body = build_chat_json(messages, tools, temperature, max_tokens, false);
    std::string response_str = post_json("/v1/chat/completions", json_body);

    ChatResponse resp;
    parse_sse_data(response_str, resp);
    return resp;
}

// ---------------------------------------------------------------------------
// Streaming chat (SSE) - template to handle both Client and SSLClient
// ---------------------------------------------------------------------------

template<typename ClientType>
ChatResponse chat_stream_impl(
    const std::vector<ChatMessage>& messages,
    TokenCallback on_token,
    const std::vector<ToolDefinition>& tools,
    double temperature,
    int max_tokens,
    const LLMClient::UrlParts& url,
    const std::string& model) {

    ClientType client(url.host, url.port);
    if (!client.is_valid()) return {};

    client.set_read_timeout(120, 0);
    client.set_write_timeout(30, 0);

    // Build JSON body — include messages and tools (was missing before).
    json req;
    req["model"] = model;
    req["temperature"] = temperature;
    req["max_tokens"] = max_tokens;
    req["stream"] = true;

    // Messages array
    json messages_arr = json::array();
    for (const auto& m : messages) {
        json msg;
        msg["role"] = m.role;
        msg["content"] = m.content;
        if (!m.name.empty()) msg["name"] = m.name;
        messages_arr.push_back(msg);
    }
    req["messages"] = messages_arr;

    // Tools array (if any)
    if (!tools.empty()) {
        json tools_arr = json::array();
        for (const auto& t : tools) {
            json tool_def;
            tool_def["type"] = "function";
            json func;
            func["name"] = t.name;
            func["description"] = t.description;
            try {
                func["parameters"] = json::parse(t.parameters_schema);
            } catch (...) {
                func["parameters"] = json::object();
            }
            tool_def["function"] = func;
            tools_arr.push_back(tool_def);
        }
        req["tools"] = tools_arr;
        req["tool_choice"] = "auto";
    }

	std::string json_body;
	try {
        json_body = req.dump();
	}
	catch (...) {}

    httplib::Headers headers;
    headers.insert({"Content-Type", "application/json"});
    auto handle = client.open_stream("POST", "/v1/chat/completions",
        {}, headers, json_body);

    if (!handle.is_valid()) return {};

    ChatResponse resp;
    std::string buffer;

    char read_buf[4096] = {0};
    bool aborted = false;
    while (!aborted) {
        ssize_t n = handle.read(read_buf, sizeof(read_buf));
        if (n <= 0) break;

        buffer.append(read_buf, static_cast<size_t>(n));

        size_t line_start = 0;
        while (true) {
            size_t nl_pos = buffer.find('\n', line_start);
            if (nl_pos == std::string::npos) break;

            std::string line = buffer.substr(line_start, nl_pos - line_start);
            line_start = nl_pos + 1;

            // SSE lines start with "data: "
            if (line.rfind("data:", 0) == 0) {
                std::string data_payload = line.substr(5);
                LLMClient::parse_sse_data(data_payload, resp);

                // Call the token callback for content and reasoning tokens
                try {
                    json data = json::parse(data_payload);
                    if (data.contains("choices") && !data["choices"].empty()) {
                        auto& choice = data["choices"][0];
                        if (choice.contains("delta")) {
                            auto& delta = choice["delta"];

                            // Reasoning/thinking tokens first
                            if (delta.contains("reasoning_content") && !delta["reasoning_content"].is_null()) {
                                std::string token = delta["reasoning_content"].get<std::string>();
                                if (!token.empty() && !on_token(token, true)) { aborted = true; break; }
                            }

                            // Normal content tokens
                            if (delta.contains("content") && !delta["content"].is_null()) {
                                std::string token = delta["content"].get<std::string>();
                                if (!token.empty() && !on_token(token, false)) { aborted = true; break; }
                            }
                        }
                    }
                } catch (...) {}
            }
        }

        if (aborted) break;

        if (line_start > 0) {
            buffer = buffer.substr(line_start);
        }
    }

    // If aborted by user, discard any partial tool calls — the response is incomplete.
    if (aborted) {
        resp.has_tool_calls = false;
        resp.tool_calls.clear();
    }

    return resp;
}

ChatResponse LLMClient::chat_stream(
    const std::vector<ChatMessage>& messages,
    TokenCallback on_token,
    const std::vector<ToolDefinition>& tools,
    double temperature,
    int max_tokens) {

    auto url = parse_url();
    if (url.host.empty()) return {};

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    if (url.is_ssl)
        return chat_stream_impl<httplib::SSLClient>(messages, on_token, tools, temperature, max_tokens, url, model_);
    else
        return chat_stream_impl<httplib::Client>(messages, on_token, tools, temperature, max_tokens, url, model_);
#else
    if (url.is_ssl) return {};
    return chat_stream_impl<httplib::Client>(messages, on_token, tools, temperature, max_tokens, url, model_);
#endif
}

// ---------------------------------------------------------------------------
// list_models - query /v1/models API
// ---------------------------------------------------------------------------

std::vector<LLMClient::ModelInfo> LLMClient::list_models() const {
    std::vector<ModelInfo> models;

    auto parts = parse_url();

    // Use plain HTTP client - local LM Studio typically runs on HTTP.
    httplib::Client client(parts.host, parts.port);
    auto res = client.Get("/v1/models");
    if (!res || res->status != 200) return models;
    try {
        auto j = json::parse(res->body);
        if (j.contains("data") && j["data"].is_array()) {
            for (const auto& item : j["data"]) {
                ModelInfo mi;
                mi.id = item.value("id", "unknown");
                mi.owned_by = item.value("owned_by", "-");

                // Try to read context_length from various API fields.
                if (item.contains("max_model_len")) {
                    try { mi.context_length = item["max_model_len"].get<int>(); } catch (...) {}
                }
                if (mi.context_length == 0 && item.contains("context_length")) {
                    try { mi.context_length = item["context_length"].get<int>(); } catch (...) {}
                }

                // If API didn't provide it, fall back to our built-in table.
                if (mi.context_length == 0)
                    mi.context_length = get_model_context_length(mi.id);

                models.push_back(mi);
            }
        }
    } catch (...) {}

    return models;
}

// Built-in context length table for common models.
// Updated when API doesn't provide the info.
int LLMClient::get_model_context_length(const std::string& model_id) {
    // Normalize: lowercase and trim whitespace.
    std::string id = model_id;
    while (!id.empty() && std::isspace(static_cast<unsigned char>(id.front()))) id.erase(id.begin());
    while (!id.empty() && std::isspace(static_cast<unsigned char>(id.back())))  id.pop_back();

    // Convert to lowercase for matching.
    std::transform(id.begin(), id.end(), id.begin(), [](unsigned char c){ return std::tolower(c); });

    struct ModelContext {
        const char* name;
        int length;
    };

    static constexpr ModelContext table[] = {
        // OpenAI
        { "gpt-4o",                  128000 },
        { "gpt-4o-mini",             128000 },
        { "gpt-4-turbo",             128000 },
        { "gpt-4",                   8192   },
        { "gpt-3.5-turbo",           16385  },
        // Claude
        { "claude-opus-4",           200000 },
        { "claude-sonnet-4",         200000 },
        { "claude-3-opus",           200000 },
        { "claude-3-sonnet",         200000 },
        { "claude-3-haiku",          200000 },
        // Anthropic (short names)
        { "opus",                    200000 },
        { "sonnet",                  200000 },
        { "haiku",                   200000 },
        // Google
        { "gemini-2.5-pro",          1048576},
        { "gemini-2.0-flash",        1048576},
        { "gemini-1.5-pro",          2097152},
        // Meta
        { "llama3.3-70b",            131072 },
        { "llama3.1-405b",           131072 },
        { "llama3.1-70b",            131072 },
        // Misc
        { "qwen-max",                32768  },
    };

    for (const auto& entry : table) {
        if (id.find(entry.name) != std::string::npos)
            return entry.length;
    }

    return 0;  // unknown
}

} // namespace agent
