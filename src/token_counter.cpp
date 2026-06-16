#include "pch.h"

#include "token_counter.h"
#include "json.hpp"

namespace agent {

// ---------------------------------------------------------------------------
// UTF-8 decoding helpers
// ---------------------------------------------------------------------------

int TokenCounter::decode_utf8_codepoint(const char* data, size_t len, size_t& pos) {
    if (pos >= len) return 0;

    unsigned char c = static_cast<unsigned char>(data[pos]);

    if ((c & 0x80) == 0) {
        // 1-byte sequence: 0xxxxxxx
        ++pos;
        return 1;
    }

    int expected_len = 0;
    if ((c & 0xE0) == 0xC0)       expected_len = 2;   // 110xxxxx
    else if ((c & 0xF0) == 0xE0) expected_len = 3;   // 1110xxxx
    else if ((c & 0xF8) == 0xF0) expected_len = 4;   // 11110xxx

    if (expected_len == 0 || pos + static_cast<size_t>(expected_len) > len) {
        return 0;  // malformed or truncated
    }

    // Validate continuation bytes.
    for (int i = 1; i < expected_len; ++i) {
        unsigned char cont = static_cast<unsigned char>(data[pos + i]);
        if ((cont & 0xC0) != 0x80) return 0;  // not a valid continuation byte
    }

    pos += static_cast<size_t>(expected_len);
    return expected_len;
}

double TokenCounter::weight_for_codepoint(uint32_t cp) {
    if (cp < 0x80) {
        // ── ASCII range ───────────────────────────────────────
        if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z'))
            return 0.25;   // letters
        if (cp >= '0' && cp <= '9')
            return 0.30;   // digits
        if (std::isspace(cp) || std::ispunct(cp))
            return 0.25;   // whitespace / punctuation
        // Remaining ASCII symbols/operators (e.g. +, -, *, =, <, >, !, @, etc.)
        return 0.60;
    }

    if (cp < 0x800) {
        // ── 2-byte UTF-8: Latin ext, Greek, Cyrillic, etc. ────
        return 0.50;
    }

    if (cp < 0x10000) {
        // ── 3-byte UTF-8: CJK, most of BMP ────────────────────
        return 1.50;
    }

    // ── 4-byte UTF-8: emoji, rare chars ───────────────────────
    return 2.00;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

size_t TokenCounter::estimate(const std::string& text) {
    double total = 0.0;
    const char* data = text.data();
    size_t len = text.size();
    size_t pos = 0;

    while (pos < len) {
        int byte_len = decode_utf8_codepoint(data, len, pos);
        if (byte_len == 0) break;  // malformed — stop counting

        uint32_t cp = 0;
        switch (byte_len) {
            case 1:
                cp = static_cast<unsigned char>(data[pos - 1]);
                break;
            case 2:
                cp = ((static_cast<uint32_t>(static_cast<unsigned char>(data[pos - 2])) & 0x1F) << 6) |
                     (static_cast<uint32_t>(static_cast<unsigned char>(data[pos - 1])) & 0x3F);
                break;
            case 3:
                cp = ((static_cast<uint32_t>(static_cast<unsigned char>(data[pos - 3])) & 0x0F) << 12) |
                     ((static_cast<uint32_t>(static_cast<unsigned char>(data[pos - 2])) & 0x3F) << 6) |
                     (static_cast<uint32_t>(static_cast<unsigned char>(data[pos - 1])) & 0x3F);
                break;
            case 4:
                cp = ((static_cast<uint32_t>(static_cast<unsigned char>(data[pos - 4])) & 0x07) << 18) |
                     ((static_cast<uint32_t>(static_cast<unsigned char>(data[pos - 3])) & 0x3F) << 12) |
                     ((static_cast<uint32_t>(static_cast<unsigned char>(data[pos - 2])) & 0x3F) << 6) |
                     (static_cast<uint32_t>(static_cast<unsigned char>(data[pos - 1])) & 0x3F);
                break;
        }

        total += weight_for_codepoint(cp);
    }

    return static_cast<size_t>(total + 0.5);  // round to nearest integer
}

size_t TokenCounter::estimate_message(const ChatMessage& msg) {
    // ~4 tokens for the role prefix overhead (e.g. "system\n", "user\n", etc.)
    constexpr size_t ROLE_OVERHEAD = 4;
    return ROLE_OVERHEAD + estimate(msg.content);
}

size_t TokenCounter::estimate_conversation(const std::vector<ChatMessage>& messages) {
    size_t total = 0;
    for (const auto& msg : messages) {
        total += estimate_message(msg);
    }
    return total;
}

std::pair<size_t, size_t> TokenCounter::from_api_usage(const std::string& json_body) {
    try {
        nlohmann::json j = nlohmann::json::parse(json_body);
        if (!j.contains("usage")) return {0, 0};

        auto usage = j["usage"];
        size_t prompt      = usage.value("prompt_tokens", 0);
        size_t completion  = usage.value("completion_tokens", 0);
        return {prompt, completion};
    } catch (...) {
        return {0, 0};
    }
}

} // namespace agent
