#pragma once

#include <iostream>
#include <string>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace agent {

// ── Severity levels ────────────────────────────────────────
enum class LogLevel {
    Debug = 0,
    Info  = 1,
    Warn  = 2,
    Error = 3,
    None  = 4   // silence everything
};

// ── Global logger state ────────────────────────────────────
inline LogLevel g_log_level = LogLevel::Info;
inline std::mutex g_log_mutex;

// Set the minimum log level. Messages below this level are suppressed.
inline void set_log_level(LogLevel level) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_log_level = level;
}

// ── Internal helpers ───────────────────────────────────────
namespace detail {

inline const char* level_tag(LogLevel lvl) {
    switch (lvl) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        default:              return "?????";
    }
}

inline const char* level_color(LogLevel lvl) {
#if defined(_WIN32) && !defined(__MINGW32__)
    // Windows MSVC supports ANSI escape codes in the new console (Win10+).
    // If you need legacy support, replace with SetConsoleTextAttribute.
#endif
    switch (lvl) {
        case LogLevel::Debug: return "\033[90m";  // grey
        case LogLevel::Info:  return "\033[32m";  // green
        case LogLevel::Warn:  return "\033[33m";  // yellow
        case LogLevel::Error: return "\033[31m";  // red
        default:              return "";
    }
}

inline const char* reset_color() {
    return "\033[0m";
}

// Format a timestamp as HH:MM:SS.fff
inline std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;

    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_buf);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

// Write a formatted log line. Caller must hold g_log_mutex or this is not thread-safe.
inline void emit(LogLevel lvl, const char* component, const std::string& msg) {
    if (lvl < g_log_level) return;

    bool use_stderr = (lvl >= LogLevel::Warn);
    auto& stream = use_stderr ? std::cerr : std::cout;

    stream << detail::level_color(lvl)
           << "[" << detail::level_tag(lvl) << "] "
           << "[" << component << "] "
           << msg
           << detail::reset_color()
           << std::endl;

    if (use_stderr) stream.flush();
}

} // namespace detail

// ── Public API: macros for convenience ─────────────────────
// Usage:  LOG_DEBUG("Component", "message");
//         LOG_INFO("Component", "message");
//         LOG_WARN("Component", "message");
//         LOG_ERROR("Component", "message");

#define LOG_DEBUG(component, msg) \
    do { std::lock_guard<std::mutex> _lg(agent::g_log_mutex); agent::detail::emit(agent::LogLevel::Debug, component, msg); } while(0)

#define LOG_INFO(component, msg) \
    do { std::lock_guard<std::mutex> _lg(agent::g_log_mutex); agent::detail::emit(agent::LogLevel::Info, component, msg); } while(0)

#define LOG_WARN(component, msg) \
    do { std::lock_guard<std::mutex> _lg(agent::g_log_mutex); agent::detail::emit(agent::LogLevel::Warn, component, msg); } while(0)

#define LOG_ERROR(component, msg) \
    do { std::lock_guard<std::mutex> _lg(agent::g_log_mutex); agent::detail::emit(agent::LogLevel::Error, component, msg); } while(0)

// ── Parse a log level string from config ───────────────────
inline LogLevel parse_log_level(const std::string& s) {
    if (s == "debug") return LogLevel::Debug;
    if (s == "info")  return LogLevel::Info;
    if (s == "warn")  return LogLevel::Warn;
    if (s == "error") return LogLevel::Error;
    if (s == "none")  return LogLevel::None;
    return LogLevel::Info; // default
}

inline std::string log_level_to_string(LogLevel lvl) {
    switch (lvl) {
        case LogLevel::Debug: return "debug";
        case LogLevel::Info:  return "info";
        case LogLevel::Warn:  return "warn";
        case LogLevel::Error: return "error";
        case LogLevel::None:  return "none";
        default:              return "info";
    }
}

} // namespace agent
