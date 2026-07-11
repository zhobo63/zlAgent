#include "pch.h"

#include "config.h"
#include "logger.h"

namespace agent {

// --- Trim helpers ---
static void ltrim(std::string& s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](char c){ return !std::isspace(c); }));
}

static void rtrim(std::string& s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](char c){ return !std::isspace(c); }).base(), s.end());
}

// --- IniParser ---

std::map<std::string, std::map<std::string, std::string>> IniParser::parse(const std::string& path) {
    std::map<std::string, std::map<std::string, std::string>> result;
    std::ifstream file(path);

    if (!file.is_open()) {
        return result; // empty = no config found
    }

    std::string current_section = "";
    std::string line;

    while (std::getline(file, line)) {
        // Trim whitespace.
        ltrim(line);
        rtrim(line);

        // Skip empty lines and comments.
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        // Section header: [section].
        if (line.front() == '[' && line.back() == ']') {
            current_section = line.substr(1, line.size() - 2);
            ltrim(current_section);
            rtrim(current_section);
            result[current_section]; // ensure section exists even with no keys.
            continue;
        }

        // Key = value or key=value.
        auto eq_pos = line.find('=');
        if (eq_pos != std::string::npos) {
            std::string key   = line.substr(0, eq_pos);
            std::string value = line.substr(eq_pos + 1);

            // Strip inline comment (; or #)
            for (size_t i = 0; i < value.size(); ++i) {
                if (value[i] == ';' || value[i] == '#') {
                    value = value.substr(0, i);
                    break;
                }
            }

            ltrim(key);
            rtrim(key);
            ltrim(value);
            rtrim(value);

            result[current_section][key] = value;
        }
    }

    return result;
}

bool IniParser::update_key(const std::string& ini_path,
                            const std::string& section,
                            const std::string& key,
                            const std::string& value) {
    // Read existing content.
    auto data = parse(ini_path);

    // Update the key in the section.
    data[section][key] = value;

    // Write back.
    std::ofstream out(ini_path);
    if (!out.is_open()) return false;

    bool first_section = true;
    for (const auto& [sec, kvs] : data) {
        if (!first_section) out << "\n";
        out << "[" << sec << "]\n";
        for (const auto& [k, v] : kvs) {
            out << k << " = " << v << "\n";
        }
        first_section = false;
    }

    out.close();
    return true;
}

// --- Config read helpers ---
// Read a key from the section map if it exists; skip silently otherwise.
static void read_if_exists(const std::map<std::string, std::string>& s,
                           const char* key, std::string& target) {
    auto it = s.find(key);
    if (it != s.end()) target = it->second;
}

static void read_if_exists(const std::map<std::string, std::string>& s,
                           const char* key, int& target) {
    auto it = s.find(key);
    if (it != s.end()) target = std::stoi(it->second);
}

static void read_if_exists(const std::map<std::string, std::string>& s,
                           const char* key, double& target) {
    auto it = s.find(key);
    if (it != s.end()) target = std::stod(it->second);
}

static void read_if_exists(const std::map<std::string, std::string>& s,
                           const char* key, float& target) {
    auto it = s.find(key);
    if (it != s.end()) target = std::stof(it->second);
}

static void read_bool(const std::map<std::string, std::string>& s,
                      const char* key, bool& target, bool default_val) {
    auto it = s.find(key);
    if (it != s.end()) target = Config::parse_bool(it->second, default_val);
}

// Read a comma-separated list of trimmed strings.
static void read_csv_list(const std::map<std::string, std::string>& s,
                          const char* key, std::vector<std::string>& target) {
    auto it = s.find(key);
    if (it == s.end()) return;

    std::string str = it->second;
    size_t pos;
    while ((pos = str.find(',')) != std::string::npos) {
        std::string item = str.substr(0, pos);
        ltrim(item);
        rtrim(item);
        if (!item.empty()) target.push_back(std::move(item));
        str = str.substr(pos + 1);
    }
    ltrim(str);
    rtrim(str);
    if (!str.empty()) target.push_back(std::move(str));
}

// Read a comma-separated list of trimmed int64 values.
static void read_csv_int_list(const std::map<std::string, std::string>& s,
                              const char* key, std::vector<int64_t>& target) {
    auto it = s.find(key);
    if (it == s.end()) return;

    std::string str = it->second;
    size_t pos;
    while ((pos = str.find(',')) != std::string::npos) {
        std::string item = str.substr(0, pos);
        ltrim(item);
        rtrim(item);
        if (!item.empty()) {
            try { target.push_back(std::stoll(item)); }
            catch (...) {}
        }
        str = str.substr(pos + 1);
    }
    ltrim(str);
    rtrim(str);
    if (!str.empty()) {
        try { target.push_back(std::stoll(str)); }
        catch (...) {}
    }
}

// --- Config helpers ---

bool Config::parse_bool(const std::string& s, bool default_val) {
    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "true" || lower == "1" || lower == "yes") return true;
    if (lower == "false" || lower == "0" || lower == "no") return false;
    return default_val;
}

Config Config::load(const std::string& ini_path) {
    Config cfg; // all defaults.

    auto data = IniParser::parse(ini_path);
    if (data.empty()) {
        LOG_INFO("Config", "No INI file found at '" + ini_path + "', using defaults.");
        Config::save(cfg, ini_path);
        return cfg;
    }

    LOG_INFO("Config", "Loaded from: " + ini_path);

    // --- [llm] section ---
    {
        auto it = data.find("llm");
        if (it != data.end()) {
            const auto& s = it->second;
            read_if_exists(s, "url",         cfg.llm.url);
            read_if_exists(s, "model",       cfg.llm.model);
            read_if_exists(s, "temperature",  cfg.llm.temperature);
            read_if_exists(s, "max_tokens",   cfg.llm.max_tokens);
        }
    }

    // --- [memory] section ---
    {
        auto it = data.find("memory");
        if (it != data.end()) {
            const auto& s = it->second;
            read_if_exists(s, "max_messages",           cfg.memory.max_messages);
            read_bool(s, "long_term_enabled",           cfg.memory.long_term_enabled, false);
            read_if_exists(s, "store_dir",              cfg.memory.store_dir);
            read_if_exists(s, "max_sessions",           cfg.memory.max_sessions);
            read_bool(s, "inject_facts_to_prompt",      cfg.memory.inject_facts_to_prompt, true);
            read_bool(s, "auto_extract_facts",          cfg.memory.auto_extract_facts, true);
        }
    }

    // --- [agent] section ---
    {
        auto it = data.find("agent");
        if (it != data.end()) {
            const auto& s = it->second;
            read_if_exists(s, "max_iterations",     cfg.agent_.max_iterations);
            read_if_exists(s, "language",           cfg.agent_.language);
            read_bool(s, "auto_detect_language",    cfg.agent_.auto_detect_language, true);
            read_if_exists(s, "prompt_file",        cfg.agent_.prompt_file);
            read_if_exists(s, "user_reply_mode",    cfg.agent_.user_reply_mode);
        }
    }

    // --- [features] section ---
    {
        auto it = data.find("features");
        if (it != data.end()) {
            const auto& s = it->second;
            read_bool(s, "task_planning",           cfg.features.task_planning, true);
            read_bool(s, "self_reflection",         cfg.features.self_reflection, true);
            read_bool(s, "multi_agent",             cfg.features.multi_agent, false);
            read_if_exists(s, "max_reflection_retries", cfg.features.max_reflection_retries);
        }
    }

    // --- [plugins] section ---
    {
        auto it = data.find("plugins");
        if (it != data.end()) {
            const auto& s = it->second;
            read_if_exists(s, "directory", cfg.plugins.directory);
        }
    }

    // --- [local_tools] section ---
    {
        auto it = data.find("local_tools");
        if (it != data.end()) {
            const auto& s = it->second;
            read_bool(s, "enabled", cfg.local_tools.enabled, true);
        }
    }

    // --- [logging] section ---
    {
        auto it = data.find("logging");
        if (it != data.end()) {
            const auto& s = it->second;
            read_if_exists(s, "level", cfg.logging.level);
        }
    }

    // --- [safety] section ---
    {
        auto it = data.find("safety");
        if (it != data.end()) {
            const auto& s = it->second;
            read_bool(s, "dangerous_tool_confirmation", cfg.safety.dangerous_tool_confirmation, true);
            read_csv_list(s, "path_whitelist",          cfg.safety.path_whitelist);
            read_if_exists(s, "working_directory",      cfg.safety.working_directory);
            read_bool(s, "strict_mode",                 cfg.safety.strict_mode, false);
            read_bool(s, "skill_content_check",         cfg.safety.skill_content_check, true);
            read_bool(s, "input_filter",                cfg.safety.input_filter, true);
        }
    }

    // --- [rag] section ---
    {
        auto it = data.find("rag");
        if (it != data.end()) {
            const auto& s = it->second;
            read_bool(s, "enabled",             cfg.rag.enabled, false);
            read_if_exists(s, "embedding_backend",   cfg.rag.embedding_backend);
            read_if_exists(s, "embedding_mode",      cfg.rag.embedding_model);
            read_if_exists(s, "store_path",          cfg.rag.store_path);
            read_if_exists(s, "top_k",               cfg.rag.top_k);
            read_if_exists(s, "min_score",           cfg.rag.min_score);
            read_csv_list(s, "knowledge_dirs",       cfg.rag.knowledge_dirs);
        }
    }

    // --- [terminal_commands] section ---
    {
        auto it = data.find("terminal_commands");
        if (it != data.end()) {
            const auto& s = it->second;
            read_bool(s, "enabled",             cfg.terminal_commands.enabled, true);
            read_csv_list(s, "direct",           cfg.terminal_commands.direct_commands);
            read_csv_list(s, "confirm",          cfg.terminal_commands.confirm_commands);
            read_bool(s, "ask_unknown",          cfg.terminal_commands.ask_unknown, false);
        }
    }

    // --- [telegram] section ---
    {
        auto it = data.find("telegram");
        if (it != data.end()) {
            const auto& s = it->second;
            read_bool(s, "enabled",              cfg.telegram.enabled, false);
            read_if_exists(s, "bot_token",       cfg.telegram.bot_token);
            read_if_exists(s, "poll_timeout_sec",  cfg.telegram.poll_timeout_sec);
            read_if_exists(s, "max_updates_per_poll", cfg.telegram.max_updates_per_poll);
            read_csv_int_list(s, "allowed_chat_ids", cfg.telegram.allowed_chat_ids);
        }
    }

    // Print loaded values.
    LOG_INFO("Config", "LLM URL: " + cfg.llm.url);
    LOG_INFO("Config", "Language: " + cfg.agent_.language
              + (cfg.agent_.prompt_file.empty() ? "" : ", Prompt file: " + cfg.agent_.prompt_file));
    LOG_INFO("Config", "Features - task_planning=" + std::to_string(cfg.features.task_planning)
              + ", self_reflection=" + std::to_string(cfg.features.self_reflection)
              + ", multi_agent=" + std::to_string(cfg.features.multi_agent)
              + ", max_reflection_retries=" + std::to_string(cfg.features.max_reflection_retries));

    return cfg;
}

bool Config::save(const Config& cfg, const std::string& ini_path) {
    std::ofstream out(ini_path);
    if (!out.is_open()) {
        LOG_ERROR("Config", "Failed to write config to '" + ini_path + "'.");
        return false;
    }

    bool first_section = true;

    auto write_section = [&](const char* name, const std::map<std::string, std::string>& kvs) {
        if (!first_section) out << "\n";
        out << "[" << name << "]\n";
        for (const auto& [k, v] : kvs) {
            out << k << " = " << v << "\n";
        }
        first_section = false;
    };

    // [llm]
    {
        std::map<std::string, std::string> kvs;
        kvs["url"]         = cfg.llm.url;
        kvs["model"]       = cfg.llm.model;
        kvs["temperature"] = std::to_string(cfg.llm.temperature);
        kvs["max_tokens"]  = std::to_string(cfg.llm.max_tokens);
        write_section("llm", kvs);
    }

    // [memory]
    {
        std::map<std::string, std::string> kvs;
        kvs["max_messages"]           = std::to_string(cfg.memory.max_messages);
        kvs["long_term_enabled"]      = cfg.memory.long_term_enabled ? "true" : "false";
        kvs["store_dir"]              = cfg.memory.store_dir;
        kvs["max_sessions"]           = std::to_string(cfg.memory.max_sessions);
        kvs["inject_facts_to_prompt"] = cfg.memory.inject_facts_to_prompt ? "true" : "false";
        kvs["auto_extract_facts"]     = cfg.memory.auto_extract_facts ? "true" : "false";
        write_section("memory", kvs);
    }

    // [agent]
    {
        std::map<std::string, std::string> kvs;
        kvs["max_iterations"]       = std::to_string(cfg.agent_.max_iterations);
        kvs["language"]             = cfg.agent_.language;
        kvs["auto_detect_language"] = cfg.agent_.auto_detect_language ? "true" : "false";
        kvs["prompt_file"]          = cfg.agent_.prompt_file;
        kvs["user_reply_mode"]      = cfg.agent_.user_reply_mode;
        write_section("agent", kvs);
    }

    // [features]
    {
        std::map<std::string, std::string> kvs;
        kvs["task_planning"]          = cfg.features.task_planning ? "true" : "false";
        kvs["self_reflection"]        = cfg.features.self_reflection ? "true" : "false";
        kvs["multi_agent"]            = cfg.features.multi_agent ? "true" : "false";
        kvs["max_reflection_retries"] = std::to_string(cfg.features.max_reflection_retries);
        write_section("features", kvs);
    }

    // [plugins]
    {
        std::map<std::string, std::string> kvs;
        kvs["directory"] = cfg.plugins.directory;
        write_section("plugins", kvs);
    }

    // [local_tools]
    {
        std::map<std::string, std::string> kvs;
        kvs["enabled"] = cfg.local_tools.enabled ? "true" : "false";
        write_section("local_tools", kvs);
    }

    // [logging]
    {
        std::map<std::string, std::string> kvs;
        kvs["level"] = cfg.logging.level;
        write_section("logging", kvs);
    }

    // [safety]
    {
        std::map<std::string, std::string> kvs;
        kvs["dangerous_tool_confirmation"] = cfg.safety.dangerous_tool_confirmation ? "true" : "false";
        if (!cfg.safety.path_whitelist.empty()) {
            std::string wl;
            for (const auto& d : cfg.safety.path_whitelist) {
                if (!wl.empty()) wl += ", ";
                wl += d;
            }
            kvs["path_whitelist"] = wl;
        }
        kvs["working_directory"]           = cfg.safety.working_directory;
        kvs["strict_mode"]                 = cfg.safety.strict_mode ? "true" : "false";
        kvs["skill_content_check"]         = cfg.safety.skill_content_check ? "true" : "false";
        kvs["input_filter"]                = cfg.safety.input_filter ? "true" : "false";
        write_section("safety", kvs);
    }

    // [rag]
    {
        std::map<std::string, std::string> kvs;
        kvs["enabled"]           = cfg.rag.enabled ? "true" : "false";
        kvs["embedding_backend"] = cfg.rag.embedding_backend;
        kvs["embedding_mode"]   = cfg.rag.embedding_model;
        kvs["store_path"]       = cfg.rag.store_path;
        kvs["top_k"]            = std::to_string(cfg.rag.top_k);
        kvs["min_score"]        = std::to_string(cfg.rag.min_score);
        if (!cfg.rag.knowledge_dirs.empty()) {
            std::string dirs;
            for (const auto& d : cfg.rag.knowledge_dirs) {
                if (!dirs.empty()) dirs += ", ";
                dirs += d;
            }
            kvs["knowledge_dirs"] = dirs;
        }
        write_section("rag", kvs);
    }

    // [terminal_commands]
    {
        std::map<std::string, std::string> kvs;
        kvs["enabled"]     = cfg.terminal_commands.enabled ? "true" : "false";
        if (!cfg.terminal_commands.direct_commands.empty()) {
            std::string cmds;
            for (const auto& c : cfg.terminal_commands.direct_commands) {
                if (!cmds.empty()) cmds += ", ";
                cmds += c;
            }
            kvs["direct"] = cmds;
        }
        if (!cfg.terminal_commands.confirm_commands.empty()) {
            std::string cmds;
            for (const auto& c : cfg.terminal_commands.confirm_commands) {
                if (!cmds.empty()) cmds += ", ";
                cmds += c;
            }
            kvs["confirm"] = cmds;
        }
        kvs["ask_unknown"] = cfg.terminal_commands.ask_unknown ? "true" : "false";
        write_section("terminal_commands", kvs);
    }

    // [telegram]
    {
        std::map<std::string, std::string> kvs;
        kvs["enabled"]            = cfg.telegram.enabled ? "true" : "false";
        kvs["bot_token"]          = cfg.telegram.bot_token;
        kvs["poll_timeout_sec"]   = std::to_string(cfg.telegram.poll_timeout_sec);
        kvs["max_updates_per_poll"] = std::to_string(cfg.telegram.max_updates_per_poll);
        if (!cfg.telegram.allowed_chat_ids.empty()) {
            std::string ids;
            for (const auto& id : cfg.telegram.allowed_chat_ids) {
                if (!ids.empty()) ids += ", ";
                ids += std::to_string(id);
            }
            kvs["allowed_chat_ids"] = ids;
        }
        write_section("telegram", kvs);
    }

    out.close();
    LOG_INFO("Config", "Default config saved to: " + ini_path);
    return true;
}

} // namespace agent
