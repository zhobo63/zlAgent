#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace agent {

/**
 * OpenAI-compatible chat message.
 */
struct ChatMessage {
    std::string role;       // "system" | "user" | "assistant" | "tool"
    std::string content;
    std::string name;       // tool name (for tool messages)
};

/**
 * Function/Tool definition sent to the LLM.
 */
struct ToolDefinition {
    std::string name;
    std::string description;
    std::string parameters_schema;  // JSON Schema string
};

/**
 * Response from the LLM.
 */
struct ChatResponse {
    std::string content;
    std::string reasoning_content;  // thinking/reasoning output from models like o1
    bool has_tool_calls = false;
    struct ToolCall {
        std::string id;
        std::string name;
        std::string arguments;  // JSON string
    };
    std::vector<ToolCall> tool_calls;

    // Token usage (0 if not available)
    size_t prompt_tokens = 0;
    size_t completion_tokens = 0;

    size_t total_tokens() const { return prompt_tokens + completion_tokens; }
};

/**
 * Callback invoked for each streaming token chunk.
 * is_reasoning = true means this token came from reasoning_content (thinking).
 * Return false to abort the stream early.
 */
using TokenCallback = std::function<bool(const std::string& token, bool is_reasoning)>;

/**
 * HTTP client that talks to LM Studio's local server (OpenAI-compatible).
 */
class LLMClient {
public:
    explicit LLMClient(const std::string& base_url = "http://127.0.0.1:1234",
                      const std::string& model = "local");

    // Non-streaming chat (blocks until full response)
    ChatResponse chat(
        const std::vector<ChatMessage>& messages,
        const std::vector<ToolDefinition>& tools = {},
        double temperature = 0.2,
        int max_tokens = 4096);

    // Streaming chat: on_token is called for each token chunk as it arrives.
    // Returns the final ChatResponse after the stream completes.
    // If on_token returns false, streaming is aborted early.
    ChatResponse chat_stream(
        const std::vector<ChatMessage>& messages,
        TokenCallback on_token,
        const std::vector<ToolDefinition>& tools = {},
        double temperature = 0.2,
        int max_tokens = 4096);

    // Build the JSON request body for chat (shared by both streaming and non-streaming)
    std::string build_chat_json(
        const std::vector<ChatMessage>& messages,
        const std::vector<ToolDefinition>& tools,
        double temperature,
        int max_tokens,
        bool stream = false);

    // Parse a single SSE "data: ..." line into content/tool_calls
    static void parse_sse_data(const std::string& data_line, ChatResponse& resp);

    // Set the model name for subsequent requests.
    void set_model(const std::string& model) { model_ = model; }

    // Get the current model name.
    const std::string& get_model() const { return model_; }

    // Get the base URL.
    const std::string& get_base_url() const { return base_url_; }

    // Query /v1/models API to list available models. Returns empty on failure.
    struct ModelInfo {
        std::string id;
        std::string owned_by;
        int context_length = 0;  // 0 means unknown
    };
    std::vector<ModelInfo> list_models() const;

    // Look up the known context length for a model. Returns 0 if unknown.
    static int get_model_context_length(const std::string& model_id);

private:
    std::string base_url_;
    std::string model_;

    // Internal HTTP POST via httplib.h (non-streaming)
    std::string post_json(const std::string& path, const std::string& json_body);

    // Extract host/port from base_url_
    struct UrlParts {
        std::string host;
        int port = 80;
        bool is_ssl = false;
    };
    UrlParts parse_url() const;
};

} // namespace agent
