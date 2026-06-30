#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>

namespace agent {

/**
 * Project Summary Tool — 自動分析 C++ 專案並生成結構化 Markdown 摘要。
 * 
 * 功能：
 *   - 掃描目錄中的 .cpp / .h 檔案
 *   - 解析 class/struct/enum/function 宣告
 *   - 建立類別階層與依賴關係圖
 *   - 識別設計模式（Singleton, Factory, Strategy 等）
 *   - 生成模組分組、API 表面、文檔覆蓋率報告
 */

// ── Forward declarations ────────────────────────────────

struct FileInfo;
struct ClassInfo;
struct FunctionInfo;
struct ModuleGroup;

/**
 * Represents a single source file with its parsed symbols.
 */
struct FileInfo {
    std::string path;              // relative path from scan root
    std::string name;              // filename without extension
    size_t line_count = 0;        // total lines in the file
    size_t comment_lines = 0;      // lines starting with // or /*
    size_t doc_comment_lines = 0;  // lines inside /** ... */ blocks

    std::string content;           // raw file contents (for pattern detection)
    std::vector<std::string> includes;   // #include directives found
    std::vector<ClassInfo> classes;      // class/struct/enum declarations
    std::vector<FunctionInfo> functions; // function/method declarations
};

/**
 * Represents a parsed class/struct/enum.
 */
struct ClassInfo {
    std::string name;
    bool is_struct = false;
    bool is_enum = false;
    bool is_class = true;
    
    std::vector<std::string> member_types;   // types of members (simplified)
    std::vector<std::string> base_classes;   // inheritance list
    
    std::set<std::string> methods;           // method names
    std::set<std::string> constructors;      // constructor signatures
    
    int line_number = 0;                     // where it's declared
};

/**
 * Represents a parsed function/method.
 */
struct FunctionInfo {
    std::string name;
    bool is_static = false;
    bool is_inline = false;
    bool is_virtual = false;
    bool is_const = false;
    
    // Simplified return type (just the first word)
    std::string return_type;
    
    int line_number = 0;
};

/**
 * Represents a group of related files forming a logical module.
 */
struct ModuleGroup {
    std::string name;              // e.g., "Core", "RAG", "TUI"
    std::vector<std::string> files; // file paths in this module
    
    int total_lines = 0;           // lines across all files
    size_t class_count = 0;        // classes defined here
    size_t function_count = 0;     // functions/methods defined here
};

/**
 * Represents a detected design pattern.
 */
struct DesignPattern {
    std::string name;              // e.g., "Singleton", "Factory"
    std::string description;       // brief explanation
    std::vector<std::string> files; // files where this pattern is found
};

/**
 * Represents a dependency edge between two classes.
 */
struct DependencyEdge {
    std::string from_class;        // class that depends
    std::string to_class;          // class being depended on
    std::string context;           // brief description of the relationship
};

// ── Project Summary Engine ──────────────────────────────

/**
 * Main engine for analyzing a C++ project.
 */
class ProjectSummaryEngine {
public:
    /**
     * Scan a directory recursively and parse all .cpp / .h files.
     * Optionally exclude certain directory names (e.g. "build", "vendor").
     */
    void scan_directory(const std::string& root_dir,
                       const std::vector<std::string>& excluded_dirs = {});

    /**
     * Generate the full Markdown summary to an output file.
     */
    bool generate_summary(const std::string& output_path) const;

    /**
     * Get a human-readable summary of what was found (for console display).
     */
    void print_preview() const;

private:
    // ── File scanning ────────────────────────────────
    
    /// Find all .cpp and .h files recursively, skipping excluded directories.
    std::vector<std::string> find_source_files(const std::string& root_dir,
                                               const std::vector<std::string>& excluded_dirs) const;
    
    /// Read a file's contents (returns empty string on error).
    std::string read_file(const std::string& path) const;

    // ── Parsing helpers ──────────────────────────────
    
    /// Parse class/struct declarations from source text.
    void parse_classes(const std::string& content, FileInfo& info);
    
    /// Parse function/method declarations from source text.
    void parse_functions(const std::string& content, FileInfo& info);

    // ── Analysis ─────────────────────────────────────
    
    /// Group files into logical modules based on naming conventions and includes.
    std::map<std::string, ModuleGroup> group_modules() const;
    
    /// Detect common design patterns in the codebase.
    std::vector<DesignPattern> detect_patterns() const;
    
    /// Build dependency graph between classes.
    std::vector<DependencyEdge> build_dependencies() const;

    // ── Statistics ────────────────────────────────────
    
    int total_files = 0;
    size_t total_lines = 0;
    size_t total_classes = 0;
    size_t total_functions = 0;
    std::set<std::string> namespaces;

    // Parsed data storage
    std::vector<FileInfo> files_;
};

// ── Convenience functions ────────────────────────────────

/**
 * Quick summary: scan directory and print preview to console.
 */
void quick_summary(const std::string& root_dir);

/**
 * Generate full Markdown report.
 */
bool generate_report(const std::string& root_dir, const std::string& output_path);

} // namespace agent
