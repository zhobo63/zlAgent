#include "pch.h"
#include "unit_test.h"
#include "memory.h"
#include <safety_guard.h>

using namespace agent;
namespace fs = std::filesystem;
using json = nlohmann::json;

static void safe_remove_all(const std::string& path)
{
    try {
        if (fs::exists(path)) fs::remove_all(path);
    } catch (...) {}
}

// ============================================================
// Memory class tests
// ============================================================

void test_memory_class(UnitReport& parent)
{
    UnitReport unit("memory_class");
    LOG_INFO("memory_class", "entry");

    // --- Test 1: Constructor with default max_messages ---
    {
        LOG_INFO("memory_class", "default_constructor");
        Memory mem;
        const auto& msgs = mem.get_messages();
        UNIT_TEST("empty_history", msgs.empty());
        UNIT_TEST("zero_cached_tokens", mem.get_cached_token_count() == 0);
    }

    // --- Test 2: Constructor with custom max_messages ---
    {
        LOG_INFO("memory_class", "custom_max_messages_constructor");
        Memory mem(100);
        const auto& msgs = mem.get_messages();
        UNIT_TEST("empty_history", msgs.empty());
        UNIT_TEST("zero_cached_tokens", mem.get_cached_token_count() == 0);
    }

    // --- Test 3: add() — single message ---
    {
        LOG_INFO("memory_class", "add_single_message");
        Memory mem;
        ChatMessage msg{"user", "Hello, world!"};
        mem.add(std::move(msg));

        const auto& msgs = mem.get_messages();
        UNIT_TEST("history_size_is_1", msgs.size() == 1);
        UNIT_TEST("role_is_user", msgs[0].role == "user");
        UNIT_TEST("content_matches", msgs[0].content == "Hello, world!");
    }

    // --- Test 4: add() — multiple messages ---
    {
        LOG_INFO("memory_class", "add_multiple_messages");
        Memory mem;
        mem.add(ChatMessage{"user", "First message"});
        mem.add(ChatMessage{"assistant", "Second message"});
        mem.add(ChatMessage{"user", "Third message"});

        const auto& msgs = mem.get_messages();
        UNIT_TEST("history_size_is_3", msgs.size() == 3);
        UNIT_TEST("first_role_user", msgs[0].role == "user");
        UNIT_TEST("second_role_assistant", msgs[1].role == "assistant");
        UNIT_TEST("third_content", msgs[2].content == "Third message");
    }

    // --- Test 5: add() — cached_tokens increases after adding messages ---
    {
        LOG_INFO("memory_class", "cached_tokens_increases");
        Memory mem;
        size_t tokens_before = mem.get_cached_token_count();
        mem.add(ChatMessage{"user", "Some content here"});
        size_t tokens_after = mem.get_cached_token_count();

        UNIT_TEST("tokens_increased", tokens_after > tokens_before);
    }

    // --- Test 6: add() — message with name field (tool messages) ---
    {
        LOG_INFO("memory_class", "add_message_with_name");
        Memory mem;
        ChatMessage msg{"tool", "Tool output", "my_tool"};
        mem.add(std::move(msg));

        const auto& msgs = mem.get_messages();
        UNIT_TEST("role_is_tool", msgs[0].role == "tool");
        UNIT_TEST("name_matches", msgs[0].name == "my_tool");
    }

    // --- Test 7: clear() — resets history and tokens ---
    {
        LOG_INFO("memory_class", "clear_resets_state");
        Memory mem;
        mem.add(ChatMessage{"user", "Before clear"});
        mem.add(ChatMessage{"assistant", "Also before clear"});

        UNIT_TEST("has_messages_before_clear", mem.get_messages().size() == 2);
        size_t tokens_before = mem.get_cached_token_count();
        UNIT_TEST("tokens_before_clear_gt_0", tokens_before > 0);

        mem.clear();

        const auto& msgs = mem.get_messages();
        UNIT_TEST("history_empty_after_clear", msgs.empty());
        UNIT_TEST("zero_tokens_after_clear", mem.get_cached_token_count() == 0);
    }

    // --- Test 8: clear() on empty memory ---
    {
        LOG_INFO("memory_class", "clear_on_empty_memory");
        Memory mem;
        mem.clear();  // should not crash
        UNIT_TEST("still_empty", mem.get_messages().empty());
        UNIT_TEST("zero_tokens", mem.get_cached_token_count() == 0);
    }

    // --- Test 9: set_system_prompt() — insert when no system prompt exists ---
    {
        LOG_INFO("memory_class", "set_system_prompt_insert");
        Memory mem;
        mem.add(ChatMessage{"user", "Hello"});

        mem.set_system_prompt("You are a helpful assistant.");

        const auto& msgs = mem.get_messages();
        UNIT_TEST("history_size_is_2", msgs.size() == 2);
        UNIT_TEST("first_msg_is_system", msgs[0].role == "system");
        UNIT_TEST("system_content_matches", msgs[0].content == "You are a helpful assistant.");
        UNIT_TEST("user_msg_still_there", msgs[1].role == "user");
    }

    // --- Test 10: set_system_prompt() — update existing system prompt ---
    {
        LOG_INFO("memory_class", "set_system_prompt_update");
        Memory mem;
        mem.set_system_prompt("Old system prompt.");
        mem.add(ChatMessage{"user", "Hello"});

        mem.set_system_prompt("New system prompt.");

        const auto& msgs = mem.get_messages();
        UNIT_TEST("history_size_is_2", msgs.size() == 2);
        UNIT_TEST("system_content_updated", msgs[0].content == "New system prompt.");
        UNIT_TEST("user_msg_still_there", msgs[1].role == "user");
    }

    // --- Test 11: set_system_prompt() — called on empty memory ---
    {
        LOG_INFO("memory_class", "set_system_prompt_empty_memory");
        Memory mem;
        mem.set_system_prompt("System prompt only.");

        const auto& msgs = mem.get_messages();
        UNIT_TEST("history_size_is_1", msgs.size() == 1);
        UNIT_TEST("system_content_matches", msgs[0].content == "System prompt only.");
    }

    // --- Test 12: set_system_prompt() — cached_tokens recalculated after update ---
    {
        LOG_INFO("memory_class", "set_system_prompt_recalculates_tokens");
        Memory mem;
        mem.set_system_prompt("Short.");
        size_t tokens_short = mem.get_cached_token_count();

        mem.set_system_prompt("This is a much longer system prompt with more words to increase the token count significantly.");
        size_t tokens_long = mem.get_cached_token_count();

        UNIT_TEST("tokens_increased_after_longer_prompt", tokens_long > tokens_short);
    }

    // --- Test 13: get_messages() returns const reference (modifications reflect) ---
    {
        LOG_INFO("memory_class", "get_messages_const_ref");
        Memory mem;
        mem.add(ChatMessage{"user", "Test"});

        const auto& msgs = mem.get_messages();
        UNIT_TEST("size_is_1", msgs.size() == 1);

        mem.add(ChatMessage{"assistant", "Reply"});
        // The reference should reflect the new state since it's a live reference.
        UNIT_TEST("size_is_2_after_add", msgs.size() == 2);
    }

    // --- Test 14: summarize() — returns false when no need to summarize (few messages) ---
    {
        LOG_INFO("memory_class", "summarize_no_need_few_messages");
        Memory mem(50);  // max 50 messages
        mem.add(ChatMessage{"user", "Hello"});
        mem.add(ChatMessage{"assistant", "Hi there!"});

        // We can't call summarize() with a real LLMClient, but we can verify
        // that the internal state is correct for when it would be called.
        const auto& msgs = mem.get_messages();
        UNIT_TEST("only_2_messages", msgs.size() == 2);
    }

    // --- Test 15: Memory with system prompt + multiple messages (typical flow) ---
    {
        LOG_INFO("memory_class", "typical_conversation_flow");
        Memory mem;
        mem.set_system_prompt("You are a coding assistant.");
        mem.add(ChatMessage{"user", "Write a function"});
        mem.add(ChatMessage{"assistant", "Here's the code..."});
        mem.add(ChatMessage{"user", "Can you explain it?"});

        const auto& msgs = mem.get_messages();
        UNIT_TEST("total_4_messages", msgs.size() == 4);
        UNIT_TEST("first_is_system", msgs[0].role == "system");
        UNIT_TEST("second_is_user", msgs[1].role == "user");
        UNIT_TEST("third_is_assistant", msgs[2].role == "assistant");
        UNIT_TEST("fourth_is_user", msgs[3].role == "user");
    }

    // --- Test 16: Multiple system prompts — only one should exist after update ---
    {
        LOG_INFO("memory_class", "only_one_system_prompt_after_update");
        Memory mem;
        mem.set_system_prompt("First prompt.");
        mem.add(ChatMessage{"user", "Hello"});
        mem.set_system_prompt("Second prompt.");

        const auto& msgs = mem.get_messages();
        int system_count = 0;
        for (const auto& m : msgs) {
            if (m.role == "system") system_count++;
        }
        UNIT_TEST("only_one_system_prompt", system_count == 1);
    }

    parent.report.push_back(unit);
}

// ============================================================
// Entry point for memory tests
// ============================================================

void test_memory(UnitReport& parent)
{
    // Ensure SafetyGuard whitelist contains current path for tests.
    auto& sg = SafetyGuard::get_instance();
    sg.set_path_whitelist({fs::current_path().string()});

    UnitReport unit("memory");
    LOG_INFO("memory", "entry");

    test_memory_class(unit);

    parent.report.push_back(unit);
}
