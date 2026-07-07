#include "pch.h"
#include "llm_client.h"
#include "key_watcher.h"
#include "logger.h"
#include "logger.h"
#include "token_counter.h"
#include <mutex>

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
    ClientType* client) {
    LOG_DEBUG("LLMClient", "POST " + client->host() + ":" + std::to_string(client->port()) + path);

    if (!client->is_valid()) {
        LOG_ERROR("LLMClient", "HTTP client invalid for " + client->host() + ":" + std::to_string(client->port()));
        return "{}";
    }

    auto res = client->Post(path, json_body, "application/json");
    if (!res) {
        LOG_ERROR("LLMClient", std::string{"Post failed: error="} +
                  std::to_string(static_cast<int>(res.error())) +
                  " body_size=" + std::to_string(json_body.size()));
        return "{}";
    }

    LOG_DEBUG("LLMClient", "Response status=" + std::to_string(res->status) +
              " body_size=" + std::to_string(res->body.size()));

    // Accept all 2xx success status codes (not just 200).
    if (res->status >= 200 && res->status < 300) {
        return res->body;
    }

    LOG_ERROR("LLMClient", std::string{"Post failed: status="} +
              std::to_string(res->status) + " body_size=" +
              std::to_string(json_body.size()) + " response=" + res->body);
    return "{}";
}

// ---------------------------------------------------------------------------
// list_models - template to handle both Client and SSLClient
// ---------------------------------------------------------------------------

template<typename ClientType>
std::vector<LLMClient::ModelInfo> list_models_impl(ClientType* client) {
    std::vector<LLMClient::ModelInfo> models;

    if (!client->is_valid()) return models;
    auto res = client->Get("/v1/models");
    if (!res || res->status != 200) return models;

    try {
        auto j = json::parse(res->body);
        if (j.contains("data") && j["data"].is_array()) {
            for (const auto& item : j["data"]) {
                LLMClient::ModelInfo mi;
                mi.id = item.value("id", "unknown");
                mi.owned_by = item.value("owned_by", "-");

                // Try to read context_length from various API fields.
                if (item.contains("max_model_len")) {
                    try { mi.context_length = item["max_model_len"].get<int>(); } catch (...) {
                        LOG_ERROR("LLMClient", "Failed to parse max_model_len for model '" + mi.id + "'");
                    }
                }
                if (mi.context_length == 0 && item.contains("context_length")) {
                    try { mi.context_length = item["context_length"].get<int>(); } catch (...) {
                        LOG_ERROR("LLMClient", "Failed to parse context_length for model '" + mi.id + "'");
                    }
                }

                // If API didn't provide it, fall back to our built-in table.
                if (mi.context_length == 0)
                    mi.context_length = LLMClient::get_model_context_length(mi.id);

                models.push_back(mi);
            }
        }
    } catch (...) {
        LOG_ERROR("LLMClient", "Failed to parse /v1/models API response");
    }

    return models;
}

std::string LLMClient::post_json(const std::string& path, const std::string& json_body) {
    auto url = parse_url();
    if (url.host.empty()) return "{}";

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    if (url.is_ssl) {
        if (!ssl_client_) {
            ssl_client_ = std::make_unique<httplib::SSLClient>(url.host, url.port);
            ssl_client_->set_read_timeout(READ_TIMEOUT, 0);
            ssl_client_->set_write_timeout(WRITE_TIMEOUT, 0);
        }
        return post_json_impl<httplib::SSLClient>(path, json_body, ssl_client_.get());
    } else {
        if (!client_) {
            client_ = std::make_unique<httplib::Client>(url.host, url.port);
            client_->set_read_timeout(READ_TIMEOUT, 0);
            client_->set_write_timeout(WRITE_TIMEOUT, 0);
        }
        return post_json_impl<httplib::Client>(path, json_body, client_.get());
    }
#else
    if (url.is_ssl) return "{}";
    if (!client_) {
        client_ = std::make_unique<httplib::Client>(url.host, url.port);
        client_->set_read_timeout(READ_TIMEOUT, 0);
        client_->set_write_timeout(WRITE_TIMEOUT, 0);
    }
    return post_json_impl<httplib::Client>(path, json_body, client_.get());
#endif
}

// ---------------------------------------------------------------------------
// Shared JSON builder using nlohmann::json
// ---------------------------------------------------------------------------

std::optional<std::string> LLMClient::build_chat_json(
    const std::string& model,
    const std::vector<ChatMessage>& messages,
    const std::vector<ToolDefinition>& tools,
    double temperature,
    int max_tokens,
    bool stream) {

    // Clamp max_tokens so it doesn't exceed the model's context window.
    // Local LLMs (LM Studio / Ollama) return 500 when max_tokens is too large.
    // We cap at ~75% of the context length to leave room for the prompt itself.
    //{
    //    int ctx_len = get_model_context_length(model);
    //    if (ctx_len > 0) {
    //        constexpr double kOutputFraction = 0.75;
    //        int safe_cap = static_cast<int>(static_cast<double>(ctx_len) * kOutputFraction);
    //        if (max_tokens > safe_cap) {
    //            LOG_WARN("LLMClient", std::string{"Clamping max_tokens from "} +
    //                     std::to_string(max_tokens) + " to " + std::to_string(safe_cap) +
    //                     " (model=" + model + ", ctx=" + std::to_string(ctx_len) + ")");
    //            max_tokens = safe_cap;
    //        }
    //    } else {
    //        // Unknown model — use a conservative default.
    //        constexpr int kDefaultCap = 8192;
    //        if (max_tokens > kDefaultCap) {
    //            LOG_WARN("LLMClient", std::string{"Clamping max_tokens from "} +
    //                     std::to_string(max_tokens) + " to " + std::to_string(kDefaultCap) +
    //                     " (unknown model: " + model + ")");
    //            max_tokens = kDefaultCap;
    //        }
    //    }
    //}

    json req;
    req["model"] = model;
    req["temperature"] = temperature;
    req["max_tokens"] = max_tokens;
    if (stream) req["stream"] = true;

    // Messages array
    json messages_arr = json::array();
    for (const auto& m : messages) {
        json msg;
        msg["role"] = m.role;
        if (!m.content.empty())
            msg["content"] = m.content;
        else
            msg["content"] = nullptr;  // null content for assistant with tool_calls only
        if (!m.name.empty()) msg["name"] = m.name;
        if (!m.tool_calls.empty()) {
            json tc_arr = json::array();
            for (const auto& tc : m.tool_calls) {
                json tc_obj;
                tc_obj["id"] = tc.id;
                tc_obj["type"] = "function";
                json func_obj;
                func_obj["name"] = tc.name;
                func_obj["arguments"] = tc.arguments;
                tc_obj["function"] = func_obj;
                tc_arr.push_back(tc_obj);
            }
            msg["tool_calls"] = tc_arr;
        }
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
            // Parse and attach the parameters schema — required by OpenAI API.
            try {
                func["parameters"] = json::parse(t.parameters_schema);
            } catch (...) {
                // Fallback: send as empty object so the request is still valid.
                func["parameters"] = json::object();
            }
            tool_def["function"] = func;
            tools_arr.push_back(tool_def);
        }
        req["tools"] = tools_arr;
        req["tool_choice"] = "auto";
    }
    std::string req_str;
    try
    {
        req_str = req.dump();
    }
    catch(const std::exception& e)
    {
        LOG_ERROR("LLMClient", "Failed to serialize chat request: " + std::string(e.what()));
        return std::nullopt;
    }

    LOG_DEBUG("LLMClient", "Chat request built, size=" + std::to_string(req_str.size()) + " bytes");

    // Warn if the request body is unusually large (potential issue with large tool calls)
    if (req_str.size() > 1024 * 1024) {  // > 1MB
        LOG_WARN("LLMClient", std::string{"Large chat request: "} + std::to_string(req_str.size()) + " bytes");
    }

    return req_str;
}

// ---------------------------------------------------------------------------
// SSE data-line parser using nlohmann::json
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// parse_sse_chunk — Parse a single SSE "data: ..." line (streaming mode).
// Handles delta tokens, reasoning content, tool calls, and usage.
// ---------------------------------------------------------------------------
bool LLMClient::parse_sse_chunk(const std::string& data_line, ChatResponse& resp, TokenCallback on_token) {
    if (data_line == "[DONE]") return true;

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
            if (!choice.contains("delta")) return true;

            auto& delta = choice["delta"];

            // Reasoning/thinking token
            if (delta.contains("reasoning_content") && !delta["reasoning_content"].is_null()) {
                std::string reasoning = delta["reasoning_content"].get<std::string>();
                resp.reasoning_content += reasoning;
                if (on_token && !reasoning.empty() && !on_token(reasoning, true))
                    return false;  // callback signaled interruption
            }

            // Content token
            if (delta.contains("content") && !delta["content"].is_null()) {
                std::string content = delta["content"].get<std::string>();
                resp.content += content;
                if (on_token && !content.empty() && !on_token(content, false))
                    return false;  // callback signaled interruption
            }

            // Tool calls in the stream chunk — merge into existing calls by index.
            // OpenAI streaming sends tool_call chunks split across SSE events:
            //   first chunk  → {index, id, function:{name}}
            //   later chunks → {index, function:{arguments}} (id may be absent)
            if (delta.contains("tool_calls")) {
                resp.has_tool_calls = true;
                //LOG_INFO("LLMClient", "Tool call detected in stream, total so far: " + std::to_string(resp.tool_calls.size()));
                for (auto& tc : delta["tool_calls"]) {
                    //LOG_DEBUG("tool_calls", tc.dump());
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
    } catch (const json::parse_error& e) {
        LOG_WARN("LLMClient", "Malformed SSE chunk ignored: " + std::string(e.what()));
    }

    return true;
}

// ---------------------------------------------------------------------------
// parse_full_response — Parse a complete non-streaming JSON response.
// Handles choices[0].message.content and tool_calls.
// ---------------------------------------------------------------------------
void LLMClient::parse_full_response(const std::string& json_str, ChatResponse& resp) {
    try {
        json data = json::parse(json_str);

        if (!data.contains("choices") || data["choices"].empty()) return;

        auto& choice = data["choices"][0];
        if (!choice.contains("message")) return;

        auto& msg = choice["message"];

        // Extract usage if present
        if (data.contains("usage")) {
            auto& u = data["usage"];
            resp.prompt_tokens      = u.value("prompt_tokens", 0u);
            resp.completion_tokens  = u.value("completion_tokens", 0u);
        }

        // Content
        if (msg.contains("content") && !msg["content"].is_null()) {
            resp.content = msg["content"].get<std::string>();
        }

        // Tool calls
        if (msg.contains("tool_calls")) {
            resp.has_tool_calls = true;
            LOG_INFO("LLMClient", "Tool calls in full response: " + std::to_string(msg["tool_calls"].size()));
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
    } catch (const json::parse_error&) {
        // Parse failure — response left empty
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

    // Auto-calculate remaining context window if not specified.
    if (max_tokens < 0) {
        size_t used = TokenCounter::estimate_conversation(messages);
        int ctx_len = get_model_context_length(model_);
        if (ctx_len > 0 && static_cast<size_t>(ctx_len) > used) {
            max_tokens = ctx_len - static_cast<int>(used);
        } else {
            max_tokens = 8192;  // safe fallback
        }
    }

    auto json_body = build_chat_json(model_, messages, tools, temperature, max_tokens, false);
    ChatResponse resp;
    resp.max_tokens = static_cast<size_t>(max_tokens);
    if (!json_body) {
        LOG_ERROR("LLMClient", "Chat request build failed, aborting");
        return resp;
    }
    std::string response_str = post_json("/v1/chat/completions", *json_body);

    parse_full_response(response_str, resp);
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
    ClientType* client,
    const std::string& model) {

    if (!client->is_valid()) return {};

    // Build JSON body using the shared helper.
    auto json_body = LLMClient::build_chat_json(model, messages, tools,
                                                       temperature, max_tokens, true);
    ChatResponse resp;
    resp.max_tokens = static_cast<size_t>(max_tokens);
    if (!json_body) {
        LOG_ERROR("LLMClient", "Chat request build failed, aborting");
        return resp;
    }
    LOG(Color::WHITE, "JSON_BODY:%s", json_body->c_str());
    LOG(Color::CYAN, "JSON_BODY SIZE:%llu", json_body->size());

    httplib::Headers headers;
    headers.insert({"Content-Type", "application/json"});

    std::unique_ptr<httplib::ClientImpl::StreamHandle> handle;

    // Wrap StreamHandle in unique_ptr so we can close the socket immediately
    // when ESC is pressed — this notifies the LLM server to stop generating.
    handle = std::make_unique<httplib::ClientImpl::StreamHandle>(
        client->open_stream("POST", "/v1/chat/completions",
            {}, headers, *json_body));

    if (!handle->is_valid()) return {};

    std::string buffer;

    char read_buf[4096] = {0};
    std::atomic<bool> was_interrupted{false};
    KeyWatcher::on_key([&](int k) {
        if (k == 27) { //ESC
            LOG_WARN("LLMClient", u8"\n⚠  Interrupted by user.");
            if(handle)
                handle->close();
            was_interrupted.store(true);
        }
    });

    while (handle && handle->is_valid()) {
        ssize_t n = 0;
        {
            n = handle->read(read_buf, sizeof(read_buf));
        }
        if (n <= 0) break;

        buffer.append(read_buf, static_cast<size_t>(n));
        //LOG(Color::GRAY, "%s", read_buf);

        size_t line_start = 0;
        while (!was_interrupted.load()) {
            size_t nl_pos = buffer.find('\n', line_start);
            if (nl_pos == std::string::npos) break;

            std::string line = buffer.substr(line_start, nl_pos - line_start);
            line_start = nl_pos + 1;

            // SSE lines start with "data: "
            if (line.rfind("data:", 0) == 0) {
                std::string data_payload = line.substr(5);
                if (!LLMClient::parse_sse_chunk(data_payload, resp, on_token)) {
                    // Token callback signaled interruption — stop generation
                    was_interrupted.store(true);
                    break;
                }
            }
        }

        if (!handle) break;  // handle was released by ESC interrupt

        if (line_start > 0) {
            buffer.erase(0, line_start);
        }
    }
    if (was_interrupted.load()) {
        buffer.clear();
        buffer.append("Interrupted by user");
    }
    KeyWatcher::clear_callback();

    // If handle was released early (ESC), discard any partial tool calls — the response is incomplete.
    if (!handle) {
        resp.has_tool_calls = false;
        resp.tool_calls.clear();
    }
	TUI::out("\n");  // ensure prompt is on a new line after streaming output
    LOG(Color::CYAN, "RESPONSE:%s", resp.content);
    LOG(Color::CYAN, "RESPONSE SIZE:%llu", resp.content.size());

    LOG_DEBUG("LLMClient", "Stream complete: has_tool_calls=" + std::to_string(resp.has_tool_calls) +
             ", tool_calls.size()=" + std::to_string(resp.tool_calls.size()));

    // If the server didn't return usage in the stream, estimate tokens ourselves.
    if (resp.prompt_tokens == 0 && !messages.empty()) {
        resp.prompt_tokens     = TokenCounter::estimate_conversation(messages);
        std::string full_output = resp.reasoning_content + resp.content;
        resp.completion_tokens = TokenCounter::estimate(full_output);
    }

    return resp;
}

ChatResponse LLMClient::chat_stream(
    const std::vector<ChatMessage>& messages,
    TokenCallback on_token,
    const std::vector<ToolDefinition>& tools,
    double temperature,
    int max_tokens) {

    // Auto-calculate remaining context window if not specified.
    if (max_tokens < 0) {
        size_t used = TokenCounter::estimate_conversation(messages);
        int ctx_len = get_model_context_length(model_);
        if (ctx_len > 0 && static_cast<size_t>(ctx_len) > used) {
            max_tokens = ctx_len - static_cast<int>(used);
        } else {
            max_tokens = 8192;  // safe fallback
        }
    }

    auto url = parse_url();
    if (url.host.empty()) return {};

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    if (url.is_ssl) {
        if (!ssl_client_) {
            ssl_client_ = std::make_unique<httplib::SSLClient>(url.host, url.port);
            ssl_client_->set_read_timeout(READ_TIMEOUT, 0);
            ssl_client_->set_write_timeout(WRITE_TIMEOUT, 0);
        }
        return chat_stream_impl<httplib::SSLClient>(messages, on_token, tools, temperature, max_tokens, ssl_client_.get(), model_);
    } else {
        if (!client_) {
            client_ = std::make_unique<httplib::Client>(url.host, url.port);
            client_->set_read_timeout(READ_TIMEOUT, 0);
            client_->set_write_timeout(WRITE_TIMEOUT, 0);
        }
        return chat_stream_impl<httplib::Client>(messages, on_token, tools, temperature, max_tokens, client_.get(), model_);
    }
#else
    if (url.is_ssl) return {};
    if (!client_) {
        client_ = std::make_unique<httplib::Client>(url.host, url.port);
        client_->set_read_timeout(READ_TIMEOUT, 0);
        client_->set_write_timeout(WRITE_TIMEOUT, 0);
    }
    return chat_stream_impl<httplib::Client>(messages, on_token, tools, temperature, max_tokens, client_.get(), model_);
#endif
}

// ---------------------------------------------------------------------------
// list_models - query /v1/models API
// ---------------------------------------------------------------------------

std::vector<LLMClient::ModelInfo> LLMClient::list_models() const {
    auto parts = parse_url();
    if (parts.host.empty()) return {};

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    if (parts.is_ssl) {
        if (!ssl_client_) {
            ssl_client_ = std::make_unique<httplib::SSLClient>(parts.host, parts.port);
            ssl_client_->set_read_timeout(READ_TIMEOUT, 0);
            ssl_client_->set_write_timeout(WRITE_TIMEOUT, 0);
        }
        return list_models_impl<httplib::SSLClient>(ssl_client_.get());
    } else {
        if (!client_) {
            client_ = std::make_unique<httplib::Client>(parts.host, parts.port);
            client_->set_read_timeout(READ_TIMEOUT, 0);
            client_->set_write_timeout(WRITE_TIMEOUT, 0);
        }
        return list_models_impl<httplib::Client>(client_.get());
    }
#else
    if (parts.is_ssl) return {};
    if (!client_) {
        client_ = std::make_unique<httplib::Client>(parts.host, parts.port);
        client_->set_read_timeout(READ_TIMEOUT, 0);
        client_->set_write_timeout(WRITE_TIMEOUT, 0);
    }
    return list_models_impl<httplib::Client>(client_.get());
#endif
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