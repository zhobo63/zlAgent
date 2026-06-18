#include "pch.h"

#include "config.h"
#include "wide_string.h"

namespace agent {

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
        auto ltrim  = [](std::string& s) { s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](char c){ return !std::isspace(c); })); };
        auto rtrim  = [](std::string& s) { s.erase(std::find_if(s.rbegin(), s.rend(), [](char c){ return !std::isspace(c); }).base(), s.end()); };

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
        std::cout << "[Config] No INI file found at '" << ini_path << "', using defaults." << std::endl;
        Config::save(cfg, ini_path);
        return cfg;
    }

    std::cout << "[Config] Loaded from: " << ini_path << std::endl;

    // --- [llm] section ---
    if (data.count("llm")) {
        auto& s = data["llm"];
        if (s.count("url"))        cfg.llm.url = s["url"];
        if (s.count("model"))      cfg.llm.model = s["model"];
        if (s.count("temperature")) cfg.llm.temperature = std::stod(s["temperature"]);
        if (s.count("max_tokens"))  cfg.llm.max_tokens = std::stoi(s["max_tokens"]);
    }

    // --- [memory] section ---
    if (data.count("memory")) {
        auto& s = data["memory"];
        if (s.count("max_messages"))         cfg.memory.max_messages = std::stoi(s["max_messages"]);
        if (s.count("long_term_enabled"))    cfg.memory.long_term_enabled = parse_bool(s["long_term_enabled"], false);
        if (s.count("store_dir"))            cfg.memory.store_dir = s["store_dir"];
        if (s.count("max_sessions"))         cfg.memory.max_sessions = std::stoi(s["max_sessions"]);
        if (s.count("inject_facts_to_prompt")) cfg.memory.inject_facts_to_prompt = parse_bool(s["inject_facts_to_prompt"], true);
        if (s.count("auto_extract_facts"))   cfg.memory.auto_extract_facts = parse_bool(s["auto_extract_facts"], true);
    }

    // --- [agent] section ---
    if (data.count("agent")) {
        auto& s = data["agent"];
        if (s.count("max_iterations")) cfg.agent_.max_iterations = std::stoi(s["max_iterations"]);
        if (s.count("language"))             cfg.agent_.language = s["language"];
        if (s.count("auto_detect_language")) cfg.agent_.auto_detect_language = parse_bool(s["auto_detect_language"], true);
        if (s.count("prompt_file"))          cfg.agent_.prompt_file = s["prompt_file"];
        if (s.count("user_reply_mode"))      cfg.agent_.user_reply_mode = s["user_reply_mode"];
    }

    // --- [features] section ---
    if (data.count("features")) {
        auto& s = data["features"];
        if (s.count("task_planning"))          cfg.features.task_planning = parse_bool(s["task_planning"], true);
        if (s.count("self_reflection"))        cfg.features.self_reflection = parse_bool(s["self_reflection"], true);
        if (s.count("multi_agent"))            cfg.features.multi_agent = parse_bool(s["multi_agent"], false);
        if (s.count("max_reflection_retries")) cfg.features.max_reflection_retries = std::stoi(s["max_reflection_retries"]);
    }

    // --- [plugins] section ---
    if (data.count("plugins")) {
        auto& s = data["plugins"];
        if (s.count("directory")) cfg.plugins.directory = s["directory"];
    }

    // --- [local_tools] section ---
    if (data.count("local_tools")) {
        auto& s = data["local_tools"];
        if (s.count("enabled")) cfg.local_tools.enabled = parse_bool(s["enabled"], true);
    }

    // --- [safety] section ---
    if (data.count("safety")) {
        auto& s = data["safety"];
        if (s.count("dangerous_tool_confirmation")) cfg.safety.dangerous_tool_confirmation = parse_bool(s["dangerous_tool_confirmation"], true);
        if (s.count("path_whitelist")) {
            // Comma-separated list of directories.
            std::string wl_str = s["path_whitelist"];
            size_t pos = 0;
            while ((pos = wl_str.find(',', pos)) != std::string::npos) {
                std::string dir = wl_str.substr(0, pos);
                // Trim.
                auto ltrim = [](std::string& d) { d.erase(d.begin(), std::find_if(d.begin(), d.end(), [](char c){ return !std::isspace(c); })); };
                auto rtrim = [](std::string& d) { d.erase(std::find_if(d.rbegin(), d.rend(), [](char c){ return !std::isspace(c); }).base(), d.end()); };
                ltrim(dir);
                rtrim(dir);
                if (!dir.empty()) cfg.safety.path_whitelist.push_back(dir);
                wl_str = wl_str.substr(pos + 1);
            }
            // Last segment.
            auto ltrim = [](std::string& d) { d.erase(d.begin(), std::find_if(d.begin(), d.end(), [](char c){ return !std::isspace(c); })); };
            auto rtrim = [](std::string& d) { d.erase(std::find_if(d.rbegin(), d.rend(), [](char c){ return !std::isspace(c); }).base(), d.end()); };
            ltrim(wl_str);
            rtrim(wl_str);
            if (!wl_str.empty()) cfg.safety.path_whitelist.push_back(wl_str);
        }
        if (s.count("skill_content_check")) cfg.safety.skill_content_check = parse_bool(s["skill_content_check"], true);
        if (s.count("input_filter"))        cfg.safety.input_filter = parse_bool(s["input_filter"], true);
    }

    // --- [rag] section ---
    if (data.count("rag")) {
        auto& s = data["rag"];
        if (s.count("enabled"))             cfg.rag.enabled = parse_bool(s["enabled"], false);
        if (s.count("embedding_backend"))   cfg.rag.embedding_backend = s["embedding_backend"];
        if (s.count("embedding_mode"))     cfg.rag.embedding_model = s["embedding_mode"];
        if (s.count("store_path"))          cfg.rag.store_path = s["store_path"];
        if (s.count("top_k"))               cfg.rag.top_k = std::stoi(s["top_k"]);
        if (s.count("min_score"))           cfg.rag.min_score = std::stof(s["min_score"]);
        if (s.count("knowledge_dirs")) {
            // Comma-separated list of directories.
            std::string dirs_str = s["knowledge_dirs"];
            size_t pos = 0;
            while ((pos = dirs_str.find(',', pos)) != std::string::npos) {
                std::string dir = dirs_str.substr(0, pos);
                auto ltrim = [](std::string& d) { d.erase(d.begin(), std::find_if(d.begin(), d.end(), [](char c){ return !std::isspace(c); })); };
                auto rtrim = [](std::string& d) { d.erase(std::find_if(d.rbegin(), d.rend(), [](char c){ return !std::isspace(c); }).base(), d.end()); };
                ltrim(dir);
                rtrim(dir);
                if (!dir.empty()) cfg.rag.knowledge_dirs.push_back(dir);
                dirs_str = dirs_str.substr(pos + 1);
            }
            auto ltrim = [](std::string& d) { d.erase(d.begin(), std::find_if(d.begin(), d.end(), [](char c){ return !std::isspace(c); })); };
            auto rtrim = [](std::string& d) { d.erase(std::find_if(d.rbegin(), d.rend(), [](char c){ return !std::isspace(c); }).base(), d.end()); };
            ltrim(dirs_str);
            rtrim(dirs_str);
            if (!dirs_str.empty()) cfg.rag.knowledge_dirs.push_back(dirs_str);
        }
    }

    // Print loaded values.
    std::cout << "[Config] LLM URL: " << cfg.llm.url << std::endl;
    std::cout << "[Config] Language: " << cfg.agent_.language
              << (cfg.agent_.prompt_file.empty() ? "" : ", Prompt file: " + cfg.agent_.prompt_file)
              << std::endl;
    std::cout << "[Config] Features - task_planning=" << std::to_string(cfg.features.task_planning)
              << ", self_reflection=" << std::to_string(cfg.features.self_reflection)
              << ", multi_agent=" << std::to_string(cfg.features.multi_agent)
              << ", max_reflection_retries=" << std::to_string(cfg.features.max_reflection_retries) << std::endl;

    return cfg;
}

bool Config::save(const Config& cfg, const std::string& ini_path) {
    std::ofstream out(ini_path);
    if (!out.is_open()) {
        std::cerr << "[Config] Failed to write config to '" << ini_path << "'." << std::endl;
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
        kvs["skill_content_check"] = cfg.safety.skill_content_check ? "true" : "false";
        kvs["input_filter"]        = cfg.safety.input_filter ? "true" : "false";
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

    out.close();
    std::cout << "[Config] Default config saved to: " << ini_path << std::endl;
    return true;
}

} // namespace agent
