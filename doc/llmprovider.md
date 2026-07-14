# LLM Provider Abstraction Plan

## 1. Problem Statement

Currently, `LLMClient` is tightly coupled to **LM Studio / OpenAI-compatible API** only:
- Hardcoded `/v1/chat/completions` endpoint
- Assumes OpenAI JSON request/response format
- No support for other providers (OpenAI cloud, Claude, local Ollama, etc.)

## 2. Proposed Architecture

```
┌─────────────────────┐
│   LLMClient         │  ← existing class (unchanged API surface)
│                     │
│  ┌───────────────┐  │
│  │ LLMProvider   │◄─┼── abstract interface
│  └───────┬───────┘  │
│          │          │
│  ┌───────┴───┐      │
│  │           │      │
│  ▼           ▼      │
│ OpenAIProvider   ▲  │
│ LMStudioProvider │  │ (default)
│ OllamaProvider   │  │
└──────────────────┘  │
```

## 3. Design Details

### 3.1 Abstract Interface: `LLMProvider`

**File:** `include/llm_provider.h`

```cpp
namespace agent {

/**
 * Abstract interface for LLM chat providers.
 * Each provider knows how to build requests, send them, and parse responses
 * for its specific backend (OpenAI, LM Studio, Ollama, etc.).
 */
class LLMProvider {
public:
    virtual ~LLMProvider() = default;

    // Build the JSON request body for this provider.
    virtual std::optional<std::string> build_request(
        const std::string& model,
        const std::vector<ChatMessage>& messages,
        const std::vector<ToolDefinition>& tools,
        double temperature,
        int max_tokens,
        bool stream) = 0;

    // Send a non-streaming request and return raw JSON response.
    virtual std::string post(const std::string& json_body) = 0;

    // Send a streaming request, invoking on_token for each chunk.
    // Returns the final ChatResponse after stream completes.
    virtual ChatResponse chat_stream(
        const std::vector<ChatMessage>& messages,
        TokenCallback on_token,
        double temperature,
        int max_tokens) = 0;

    // Parse a full non-streaming JSON response into ChatResponse.
    static void parse_full_response(const std::string& json_str, ChatResponse& resp);

    // Parse a single SSE data-line chunk (shared across providers).
    static bool parse_sse_chunk(const std::string& data_line, ChatResponse& resp, TokenCallback on_token = nullptr);

    // List available models for this provider.
    virtual std::vector<LLMClient::ModelInfo> list_models() = 0;

    // Get the provider name (e.g., "openai", "lmstudio", "ollama").
    virtual const char* name() const = 0;
};

} // namespace agent
```

### 3.2 Concrete Providers

**File:** `include/openai_provider.h` / `src/openai_provider.cpp`
- Base URL: `https://api.openai.com/v1/...`
- Requires API key (via env var or constructor param)
- Standard OpenAI JSON format

**File:** `include/lmstudio_provider.h` / `src/lmstudio_provider.cpp`
- Base URL: configurable (default `http://127.0.0.1:1234`)
- No auth header needed
- Same JSON format as OpenAI (LM Studio is compatible)

**File:** `include/ollama_provider.h` / `src/ollama_provider.cpp`
- Base URL: configurable (default `http://127.0.0.1:11434`)
- Different endpoint (`/api/chat`) and slightly different JSON format
- Streaming via SSE

### 3.3 LLMClient Integration

**File:** `include/llm_client.h` — add provider field:

```cpp
class LLMClient {
public:
    // Factory method to create a default provider based on URL scheme.
    static std::shared_ptr<LLMProvider> make_default_provider(const std::string& base_url);

    explicit LLMClient(std::shared_ptr<LLMProvider> provider, const std::string& model = "local");

    // Existing public API remains unchanged — delegates to provider_ internally.
    ChatResponse chat(...) { return provider_->chat_stream_impl(...); }  // or new non-stream method
    ChatResponse chat_stream(...) { ... }
    // ... rest unchanged
private:
    std::shared_ptr<LLMProvider> provider_;
};
```

## 4. Refactoring Strategy (Incremental, No Breaking Changes)

| Step | Action | Risk |
|------|--------|------|
| 1 | Create `include/llm_provider.h` with abstract interface | Low — new file only |
| 2 | Move existing `build_chat_json`, `parse_sse_chunk`, `parse_full_response` to static methods on `LLMProvider` (or keep as-is since they're already shared) | Low |
| 3 | Create `LMStudioProvider` by extracting current `LLMClient` logic into a provider implementation | Medium — refactor existing code |
| 4 | Add `make_default_provider()` factory in `LLMClient` that detects URL and returns appropriate provider | Low |
| 5 | Modify `LLMClient` to hold `shared_ptr<LLMProvider>` and delegate calls | Medium — internal change only, public API unchanged |
| 6 | Create `OpenAIProvider` (optional, for future use) | Low |

## 5. Key Considerations

1. **Backward compatibility**: `LLMClient` constructor signature stays the same (`base_url`, `model`). Existing code compiles without changes.

2. **HTTP client reuse**: Each provider manages its own `httplib::Client`/`SSLClient`. The factory creates one shared instance per `LLMClient`.

3. **Token counting**: `TokenCounter::estimate_conversation()` stays in `llm_client.cpp` as a utility — not provider-specific.

4. **Key watcher / ESC interrupt**: This is UI-layer concern, should stay in `chat_stream()` wrapper (not inside providers).

5. **No new dependencies**: Still uses only `httplib.h`, `nlohmann/json.hpp`, and existing headers.

## 6. Files to Create/Modify

| File | Action |
|------|--------|
| `include/llm_provider.h` | **Create** — abstract interface + static helpers |
| `src/llm_provider.cpp` | **Create** — shared SSE parser, full response parser |
| `include/lmstudio_provider.h` | **Create** — LM Studio provider impl |
| `src/lmstudio_provider.cpp` | **Create** — extracted from current `LLMClient::post_json`, `chat_stream_impl` |
| `include/llm_client.h` | **Modify** — add `make_default_provider()`, store `shared_ptr<LLMProvider>` |
| `src/llm_client.cpp` | **Modify** — delegate to provider, remove duplicate HTTP logic |

## 7. Implementation Priority

1. ✅ Step 1: Create `llm_provider.h` interface
2. ✅ Step 2-3: Extract current logic into `LMStudioProvider`
3. ✅ Step 4-5: Wire up in `LLMClient` with factory
4. ⏭️ Step 6: Add `OpenAIProvider` (when needed)
