#include "pch.h"

#include "long_term_memory.h"
#include "memory.h"
#include "rag_manager.h"
#include <ctime>
#include <iomanip>
#include "json.hpp"

namespace agent {
namespace fs = std::filesystem;
using json = nlohmann::json;

// Global long-term memory pointer (set by main.cpp, used by memory tools).
static LongTermMemory* g_long_term_memory = nullptr;

void set_global_long_term_memory(LongTermMemory* ltm) {
    g_long_term_memory = ltm;
}

LongTermMemory* get_global_long_term_memory() {
    return g_long_term_memory;
}

// ============================================================================
// LongTermMemory
// ============================================================================

LongTermMemory::LongTermMemory(const Config& cfg) : cfg_(cfg) {}

std::string LongTermMemory::current_timestamp() {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

// ── Session Management ────────────────────────────────

std::string LongTermMemory::generate_summary(
    const std::vector<ChatMessage>& messages, LLMClient& llm) {

    ChatMessage sys_msg{"system",
        "You are a session summarizer. Summarize the following conversation into:\n"
        "- A one-line topic (e.g., 'Implemented RAG vector store')\n"
        "- 3-5 bullet points covering key actions, decisions, and outcomes\n\n"
        "Respond in this exact format:\n"
        "TOPIC: <one line>\n"
        "SUMMARY:\n- ...\n- ..."};

    std::vector<ChatMessage> prompt;
    prompt.push_back(sys_msg);

    // Include a representative sample of the conversation.
    for (const auto& m : messages) {
        if (m.role == "system") continue;  // skip system prompts
        std::string content = m.content;
        if (content.size() > 1024) {
            content = content.substr(0, 1024) + "... [truncated]";
        }
        prompt.push_back(ChatMessage{m.role, content, m.name});
    }

    auto resp = llm.chat(prompt);
    if (resp.content.empty()) return "No summary available.";

    // Parse TOPIC and SUMMARY from the response.
    std::string topic = "General conversation";
    std::string summary = resp.content;

    size_t topic_pos = resp.content.find("TOPIC:");
    size_t summary_pos = resp.content.find("SUMMARY:");

    if (topic_pos != std::string::npos && summary_pos != std::string::npos) {
        // Extract topic line.
        size_t topic_start = topic_pos + 6;
        size_t topic_end = resp.content.find('\n', topic_start);
        if (topic_end == std::string::npos) topic_end = resp.content.size();
        topic = resp.content.substr(topic_start, topic_end - topic_start);

        // Extract summary.
        size_t sum_start = summary_pos + 8;
        summary = resp.content.substr(sum_start);
    } else if (topic_pos != std::string::npos) {
        size_t topic_start = topic_pos + 6;
        size_t topic_end = resp.content.find('\n', topic_start);
        if (topic_end == std::string::npos) topic_end = resp.content.size();
        topic = resp.content.substr(topic_start, topic_end - topic_start);
    }

    // Trim whitespace.
    auto trim = [](std::string& s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  s.pop_back();
    };
    trim(topic);
    trim(summary);

    return topic + "\n" + summary;
}

std::vector<std::pair<std::string, std::string>> LongTermMemory::extract_facts(
    const std::vector<ChatMessage>& messages, LLMClient& llm) {

    ChatMessage sys_msg{"system",
        "Extract key facts from this conversation as a JSON array of objects with 'key' and 'value'.\n"
        "Focus on: project configuration, coding preferences, architectural decisions,\n"
        "user preferences, tool usage patterns.\n\n"
        "Example keys: project.build_system, coding.style, user.preference.<name>\n\n"
        "Respond with ONLY valid JSON. No other text."};

    std::vector<ChatMessage> prompt;
    prompt.push_back(sys_msg);

    for (const auto& m : messages) {
        if (m.role == "system") continue;
        std::string content = m.content;
        if (content.size() > 1024) {
            content = content.substr(0, 1024) + "... [truncated]";
        }
        prompt.push_back(ChatMessage{m.role, content, m.name});
    }

    auto resp = llm.chat(prompt);
    if (resp.content.empty()) return {};

    // Parse JSON array.
    std::vector<std::pair<std::string, std::string>> facts;
    try {
        auto j = json::parse(resp.content);
        if (!j.is_array()) return {};
        for (const auto& item : j) {
            if (item.contains("key") && item.contains("value")) {
                std::string key = item["key"].get<std::string>();
                std::string value = item["value"].get<std::string>();
                if (!key.empty() && !value.empty()) {
                    facts.push_back({std::move(key), std::move(value)});
                }
            }
        }
    } catch (...) {
        std::cerr << "[LongTermMemory] JSON parse failed - no facts extracted from conversation" << std::endl;
        // JSON parse failed - no facts extracted.
    }

    return facts;
}

void LongTermMemory::save_session(Memory& working_memory, LLMClient& llm) {
    const auto& messages = working_memory.get_messages();
    if (messages.empty()) return;

    std::string ts = current_timestamp();

    // Generate summary.
    std::string raw_summary = generate_summary(messages, llm);

    SessionSummary session;
    session.timestamp = ts;
    session.message_count = static_cast<int>(messages.size());

    // Parse topic and summary from the combined string.
    size_t nl_pos = raw_summary.find('\n');
    if (nl_pos != std::string::npos) {
        session.topic = raw_summary.substr(0, nl_pos);
        session.summary = raw_summary.substr(nl_pos + 1);
    } else {
        session.topic = "General conversation";
        session.summary = raw_summary;
    }

    // Insert at the front (newest first).
    sessions_.insert(sessions_.begin(), std::move(session));

    // Trim to max_sessions.
    while (static_cast<int>(sessions_.size()) > cfg_.max_sessions) {
        sessions_.pop_back();
    }

    // Extract facts if enabled.
    if (cfg_.auto_extract_facts) {
        auto extracted = extract_facts(messages, llm);
        for (const auto& [key, value] : extracted) {
            FactEntry entry;
            entry.key = key;
            entry.value = value;
            entry.source_session = ts;
            entry.timestamp = ts;
            facts_[key] = std::move(entry);
        }
    }

    // Persist to disk.
    save();
}

std::vector<SessionSummary> LongTermMemory::get_recent_sessions(int n) const {
    if (n <= 0 || sessions_.empty()) return {};
    int count = std::min(n, static_cast<int>(sessions_.size()));
    return std::vector<SessionSummary>(sessions_.begin(), sessions_.begin() + count);
}

// ── Semantic Facts ────────────────────────────────────

void LongTermMemory::add_fact(const std::string& key, const std::string& value) {
    FactEntry entry;
    entry.key = key;
    entry.value = value;
    entry.timestamp = current_timestamp();
    facts_[key] = std::move(entry);
}

std::vector<FactEntry> LongTermMemory::get_facts(const std::string& prefix) const {
    std::vector<FactEntry> result;
    for (const auto& [k, v] : facts_) {
        if (prefix.empty() || k.substr(0, prefix.size()) == prefix) {
            result.push_back(v);
        }
    }
    return result;
}

void LongTermMemory::remove_fact(const std::string& key) {
    facts_.erase(key);
}

// ── Persistence ───────────────────────────────────────

bool LongTermMemory::load() {
    if (!fs::exists(cfg_.store_dir)) return false;

    // Load sessions.
    auto sessions_path = cfg_.store_dir + "/sessions.json";
    if (fs::exists(sessions_path)) {
        std::ifstream in(sessions_path);
        try {
            json root = json::parse(in);
            if (root.contains("sessions") && root["sessions"].is_array()) {
                for (const auto& s : root["sessions"]) {
                    SessionSummary ss;
                    ss.timestamp = s.value("timestamp", "");
                    ss.topic = s.value("topic", "General conversation");
                    ss.summary = s.value("summary", "");
                    ss.message_count = s.value("message_count", 0);
                    sessions_.push_back(ss);
                }
            }
        } catch (...) {
            std::cerr << "[LongTermMemory] Failed to parse sessions.json" << std::endl;
        }
    }

    // Load facts.
    auto facts_path = cfg_.store_dir + "/facts.json";
    if (fs::exists(facts_path)) {
        std::ifstream in(facts_path);
        try {
            json root = json::parse(in);
            if (root.contains("facts") && root["facts"].is_array()) {
                for (const auto& f : root["facts"]) {
                    FactEntry entry;
                    entry.key = f.value("key", "");
                    entry.value = f.value("value", "");
                    entry.source_session = f.value("source_session", "");
                    entry.timestamp = f.value("timestamp", "");
                    if (!entry.key.empty()) {
                        facts_[entry.key] = std::move(entry);
                    }
                }
            }
        } catch (...) {
            std::cerr << "[LongTermMemory] Failed to parse facts.json" << std::endl;
        }
    }

    return !sessions_.empty() || !facts_.empty();
}

void LongTermMemory::save() const {
    // Ensure directory exists.
    fs::create_directories(cfg_.store_dir);

    // Save sessions.
    json sessions_json;
    for (const auto& s : sessions_) {
        json sj;
        sj["timestamp"] = s.timestamp;
        sj["topic"] = s.topic;
        sj["summary"] = s.summary;
        sj["message_count"] = s.message_count;
        sessions_json["sessions"].push_back(sj);
    }

    auto sessions_path = cfg_.store_dir + "/sessions.json";
    std::ofstream out_sessions(sessions_path);
    if (out_sessions.is_open()) {
        out_sessions << sessions_json.dump(2);
        out_sessions.close();
    }

    // Save facts.
    json facts_json;
    for (const auto& [k, v] : facts_) {
        json fj;
        fj["key"] = k;
        fj["value"] = v.value;
        fj["source_session"] = v.source_session;
        fj["timestamp"] = v.timestamp;
        facts_json["facts"].push_back(fj);
    }

    auto facts_path = cfg_.store_dir + "/facts.json";
    std::ofstream out_facts(facts_path);
    if (out_facts.is_open()) {
        out_facts << facts_json.dump(2);
        out_facts.close();
    }
}

std::string LongTermMemory::build_context_string(int recent_n) const {
    std::ostringstream oss;

    // Semantic facts.
    if (!facts_.empty()) {
        oss << "## Long-Term Memory: Semantic Facts\n";
        for (const auto& [k, v] : facts_) {
            oss << "- **" << k << "** = " << v.value << "\n";
        }
        oss << "\n";
    }

    // Recent sessions.
    if (!sessions_.empty()) {
        int count = std::min(recent_n, static_cast<int>(sessions_.size()));
        oss << "## Recent Sessions (last " << count << ")\n";
        for (int i = 0; i < count; ++i) {
            const auto& s = sessions_[i];
            // Extract date from timestamp.
            std::string date = s.timestamp.substr(0, 10);
            oss << (i + 1) << ". [" << date << "] **" << s.topic
                << "** - " << s.summary.substr(0, 200) << "\n";
        }
    }

    return oss.str();
}

// ── RAG Integration ───────────────────────────────────

void LongTermMemory::integrate_with_rag(RAGManager* rag_manager) {
    if (!rag_manager || sessions_.empty()) return;

    for (const auto& s : sessions_) {
        std::ostringstream doc;
        doc << "Session: " << s.topic << "\n";
        doc << "Date: " << s.timestamp << "\n";
        doc << "Messages: " << s.message_count << "\n\n";
        doc << "Summary:\n" << s.summary;

        rag_manager->add_document(doc.str(), "session:" + s.timestamp);
    }
}

} // namespace agent
