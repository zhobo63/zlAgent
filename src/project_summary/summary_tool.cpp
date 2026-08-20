#include "summary_tool.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>
#include <filesystem>
#include <iterator>

namespace fs = std::filesystem;
using namespace agent;

// ── Helper: read file contents ────────────────────────────

std::string ProjectSummaryEngine::read_file(const std::string& path) const {
    try {
        std::ifstream file(path);
        if (!file.is_open()) return "";

        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    } catch (...) {
        return "";
    }
}

// ── Helper: find source files recursively ─────────────────

std::vector<std::string> ProjectSummaryEngine::find_source_files(const std::string& root_dir,
                                                                 const std::vector<std::string>& excluded_dirs) const {
    std::vector<std::string> result;

    // Normalize excluded dirs to lowercase for case-insensitive comparison
    std::vector<std::string> excluded_lower;
    for (const auto& d : excluded_dirs) {
        std::string lower = d;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        excluded_lower.push_back(lower);
    }

    try {
        for (const auto& entry : fs::recursive_directory_iterator(root_dir)) {
            if (!entry.is_regular_file()) continue;

            // Check if any parent directory is in the exclusion list
            bool skip = false;
            for (const auto& excl : excluded_lower) {
                std::string path_str = entry.path().string();
                std::transform(path_str.begin(), path_str.end(), path_str.begin(), ::tolower);
                if (path_str.find(excl) != std::string::npos) {
                    skip = true;
                    break;
                }
            }
            if (skip) continue;

            std::string ext = entry.path().extension().string();
            if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" ||
                ext == ".h" || ext == ".hpp" || ext == ".hh") {
                // Make path relative to root_dir
                std::string rel_path = fs::relative(entry.path(), root_dir).string();
                result.push_back(rel_path);
            }
        }
    } catch (const std::exception& e) {
        // Silently ignore errors during scanning
    }

    return result;
}

// ── Parsing: classes/structs/enums ────────────────────────

void ProjectSummaryEngine::parse_classes(const std::string& content, FileInfo& info) {
    // Match patterns like:
    //   class ClassName : public BaseClass {
    //   struct StructName {
    //   enum EnumName {

    static const std::regex class_regex(
        R"(class\s+(\w+)\s*(?::\s*public\s+(?:[\w<>\s&*,]+)*)\s*\{)"
    );
    static const std::regex struct_regex(
        R"(struct\s+(\w+)\s*(?::\s*public\s+(?:[\w<>\s&*,]+)*)?\s*\{)"
    );
    static const std::regex enum_regex(
        R"(enum\s+(?:class|struct)\s+(\w+)\s*\{)"
    );

    // Simple line-by-line parsing for class/struct declarations
    size_t current_line = 0;
    bool in_class_block = false;
    ClassInfo current_class;

    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line)) {
        ++current_line;

        // Check for class/struct declaration start
        if (line.find("class ") == 0 || line.find("struct ") == 0) {
            in_class_block = true;

            // Extract name and bases
            std::string trimmed = line.substr(line.find_first_not_of(" \t"));

            if (trimmed.find("class") == 0) {
                current_class.is_struct = false;
                current_class.is_enum = false;
                current_class.is_class = true;

                // Extract class name
                size_t pos = trimmed.find(' ');
                if (pos != std::string::npos) {
                    current_class.name = trimmed.substr(pos + 1);
                } else {
                    current_class.name = trimmed;
                }
            } else if (trimmed.find("struct") == 0) {
                current_class.is_struct = true;
                current_class.is_enum = false;
                current_class.is_class = false;

                size_t pos = trimmed.find(' ');
                if (pos != std::string::npos) {
                    current_class.name = trimmed.substr(pos + 1);
                } else {
                    current_class.name = trimmed;
                }
            }

            // Extract base classes
            size_t brace_pos = line.find('{');
            if (brace_pos != std::string::npos) {
                std::string before_brace = line.substr(0, brace_pos);

                // Look for : public or just : followed by bases
                size_t colon_pos = before_brace.rfind(':');
                if (colon_pos != std::string::npos && colon_pos > 0) {
                    std::string after_colon = before_brace.substr(colon_pos + 1);

                    // Remove "public" keyword if present
                    size_t pub_pos = after_colon.find("public");
                    if (pub_pos != std::string::npos) {
                        after_colon = after_colon.substr(pub_pos + 6);
                    } else {
                        pub_pos = after_colon.find("protected");
                        if (pub_pos != std::string::npos) {
                            after_colon = after_colon.substr(pub_pos + 10);
                        } else {
                            pub_pos = after_colon.find("private");
                            if (pub_pos != std::string::npos) {
                                after_colon = after_colon.substr(pub_pos + 8);
                            }
                        }
                    }

                    // Trim whitespace and split by comma
                    std::istringstream base_stream(after_colon);
                    std::string base;
                    while (std::getline(base_stream, base, ',')) {
                        // Trim
                        size_t start = base.find_first_not_of(" \t");
                        if (start != std::string::npos) {
                            current_class.base_classes.push_back(base.substr(start));
                        }
                    }
                }
            }

            info.classes.push_back(current_class);
        } else if (in_class_block && line.find('{') == 0 ||
                   in_class_block && line.find('{') != std::string::npos) {
            // Found opening brace of class body, now parse members until closing brace
            size_t brace_pos = line.find('{');
            if (brace_pos != std::string::npos) {
                std::string before_brace = line.substr(0, brace_pos);

                // Parse type and name from "type name;" or just "name;"
                std::istringstream member_stream(before_brace);
                std::string member;
                while (std::getline(member_stream, member, ';')) {
                    if (member.empty()) continue;

                    // Trim
                    size_t start = member.find_first_not_of(" \t");
                    if (start == std::string::npos) continue;
                    member = member.substr(start);

                    // Skip empty or comments
                    if (member.empty() || member[0] == '/' || member[0] == '*') continue;

                    // Check for constructor pattern: ClassName(...) : ... {
                    if (member.find(current_class.name) != std::string::npos &&
                        member.find('(') != std::string::npos) {
                        current_class.constructors.insert(member);
                    } else {
                        // Extract type and name
                        size_t space_pos = member.find(' ');
                        if (space_pos != std::string::npos) {
                            std::string type = member.substr(0, space_pos);
                            std::string name = member.substr(space_pos + 1);

                            // Trim name
                            size_t nstart = name.find_first_not_of(" \t");
                            if (nstart != std::string::npos) {
                                name = name.substr(nstart);
                            }

                            if (!name.empty() && name[0] != '/' && name[0] != '*') {
                                current_class.member_types.push_back(type + " " + name);
                            }
                        } else {
                            // Just a type (like enum values)
                            current_class.member_types.push_back(member);
                        }
                    }
                }

                in_class_block = false;
            }
        }
    }
}

// ── Parsing: functions/methods ────────────────────────────

void ProjectSummaryEngine::parse_functions(const std::string& content, FileInfo& info) {
    // Match patterns like:
    //   void function_name() {
    //   int MyClass::method_name() const {

    static const std::regex func_regex(
        R"(^\s*(static\s+|inline\s+|virtual\s+)?(\w+(?:\s*\*?\s*)?)\s+(\w+)\s*(?::\s*[\w<>\s&*,]+)::*\s+\w+\s*\([^)]*\)\s*(const\s*)?\{)"
    );

    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line)) {
        // Check for function declaration pattern
        if (line.find("::{") != std::string::npos &&
            line.find('(') != std::string::npos &&
            line.find('{') != std::string::npos) {

            FunctionInfo func;

            // Extract return type and name
            size_t brace_pos = line.rfind('{');
            if (brace_pos == 0) continue;

            std::string before_brace = line.substr(0, brace_pos);

            // Find the last :: to separate class from method
            auto last_colon = before_brace.rfind("::");
            if (last_colon != std::string::npos && last_colon > 0) {
                func.name = before_brace.substr(last_colon + 2);

                // Extract return type (everything before the class name)
                size_t space_pos = before_brace.find_last_of(" \t");
                if (space_pos != std::string::npos && space_pos > last_colon) {
                    func.return_type = before_brace.substr(0, space_pos + 1);
                } else {
                    // Try to get return type from beginning
                    size_t keyword_end = before_brace.find_first_of("static inline virtual");
                    if (keyword_end != std::string::npos) {
                        func.return_type = before_brace.substr(keyword_end);
                    }
                }

                // Check modifiers
                if (before_brace.find("static") == 0 ||
                    before_brace.find("static ") == 0) {
                    func.is_static = true;
                }
                if (before_brace.find("inline") != std::string::npos &&
                    before_brace.find("inline ") != std::string::npos) {
                    func.is_inline = true;
                }
                if (before_brace.find("virtual") != std::string::npos &&
                    before_brace.find("virtual ") != std::string::npos) {
                    func.is_virtual = true;
                }
                if (before_brace.find("const") != std::string::npos &&
                    before_brace.find(" const") != std::string::npos) {
                    func.is_const = true;
                }

                info.functions.push_back(func);
            }
        }
    }
}

// ── Module grouping ───────────────────────────────────────

std::map<std::string, ModuleGroup> ProjectSummaryEngine::group_modules() const {
    std::map<std::string, ModuleGroup> modules;

    // Simple heuristic: group by directory structure and naming patterns
    for (const auto& file : files_) {
        // Determine module name from path components
        std::string module_name;
        std::istringstream path_stream(file.path);
        std::string component;
        bool first = true;

        while (std::getline(path_stream, component, '/')) {
            if (!first) {
                module_name += "_";
            }

            // Convert to uppercase for readability
            std::string upper;
            std::transform(component.begin(), component.end(), std::back_inserter(upper), ::toupper);

            // Remove extension
            size_t dot_pos = upper.find('.');
            if (dot_pos != std::string::npos) {
                upper = upper.substr(0, dot_pos);
            }

            module_name += upper;
            first = false;
        }

        // Default to "Core" if no clear structure
        if (module_name.empty()) {
            module_name = "Core";
        }

        auto& mod = modules[module_name];
        mod.name = module_name;
        mod.files.push_back(file.path);
        mod.total_lines += static_cast<int>(file.line_count);
        mod.class_count += file.classes.size();
        mod.function_count += file.functions.size();
    }

    return modules;
}

// ── Design pattern detection ──────────────────────────────

std::vector<DesignPattern> ProjectSummaryEngine::detect_patterns() const {
    std::vector<DesignPattern> patterns;

    // Check for Singleton pattern (static getInstance methods)
    for (const auto& file : files_) {
        if (file.content.find("getInstance") != std::string::npos ||
            file.content.find("get_instance") != std::string::npos) {

            DesignPattern pattern;
            pattern.name = "Singleton";
            pattern.description = "Static factory method for singleton instance creation";
            pattern.files.push_back(file.path);
            patterns.push_back(pattern);
        }
    }

    // Check for Factory pattern (create* methods)
    for (const auto& file : files_) {
        if (file.content.find("create") != std::string::npos &&
            file.content.find("factory") != std::string::npos) {

            DesignPattern pattern;
            pattern.name = "Factory";
            pattern.description = "Object creation via factory methods";
            pattern.files.push_back(file.path);
            patterns.push_back(pattern);
        }
    }

    // Check for Strategy pattern (multiple implementations of same interface)
    for (const auto& file : files_) {
        if (file.content.find("Strategy") != std::string::npos ||
            file.content.find("strategy") != std::string::npos) {

            DesignPattern pattern;
            pattern.name = "Strategy";
            pattern.description = "Pluggable algorithm selection via interface";
            pattern.files.push_back(file.path);
            patterns.push_back(pattern);
        }
    }

    // Check for Observer pattern (event listeners/callbacks)
    for (const auto& file : files_) {
        if (file.content.find("on_event") != std::string::npos ||
            file.content.find("addListener") != std::string::npos) {

            DesignPattern pattern;
            pattern.name = "Observer";
            pattern.description = "Event-driven callback mechanism";
            pattern.files.push_back(file.path);
            patterns.push_back(pattern);
        }
    }

    // Check for Repository pattern (data access abstraction)
    for (const auto& file : files_) {
        if (file.content.find("Repository") != std::string::npos ||
            file.content.find("repository") != std::string::npos) {

            DesignPattern pattern;
            pattern.name = "Repository";
            pattern.description = "Data access abstraction layer";
            pattern.files.push_back(file.path);
            patterns.push_back(pattern);
        }
    }

    return patterns;
}

// ── Dependency graph building ─────────────────────────────

std::vector<DependencyEdge> ProjectSummaryEngine::build_dependencies() const {
    std::vector<DependencyEdge> edges;

    // Simple heuristic: if file A includes file B, there's a dependency
    for (const auto& file : files_) {
        for (const auto& include : file.includes) {
            DependencyEdge edge;
            edge.from_class = file.name + "_module";
            edge.to_class = include.substr(0, include.find('.')); // Remove path prefix

            edges.push_back(edge);
        }
    }

    return edges;
}

// ── Main scan function ────────────────────────────────────

void ProjectSummaryEngine::scan_directory(const std::string& root_dir,
                                          const std::vector<std::string>& excluded_dirs) {
    auto source_files = find_source_files(root_dir, excluded_dirs);

    for (const auto& file_path : source_files) {
        FileInfo info;
        info.path = file_path;

        // Read and parse the file
        std::string content = read_file(file_path);
        if (!content.empty()) {
            info.content = content;
            info.line_count = std::count(content.begin(), content.end(), '\n') + 1;

            // Count comments (simplified: count lines starting with // or /*)
            size_t comment_count = 0;
            std::istringstream cstream(content);
            std::string cline;
            bool in_block_comment = false;
            while (std::getline(cstream, cline)) {
                std::string trimmed = cline;
                size_t start = trimmed.find_first_not_of(" \t");
                if (start != std::string::npos) {
                    trimmed = trimmed.substr(start);
                } else {
                    trimmed.clear();
                }

                if (in_block_comment) {
                    ++comment_count;
                    if (trimmed.find("*/") != std::string::npos) {
                        in_block_comment = false;
                    }
                } else if (trimmed.substr(0, 2) == "//") {
                    ++comment_count;
                } else if (trimmed.substr(0, 2) == "/*") {
                    ++comment_count;
                    if (trimmed.find("*/", 2) == std::string::npos) {
                        in_block_comment = true;
                    }
                }
            }
            info.comment_lines = comment_count;

            parse_classes(content, info);
            parse_functions(content, info);

            files_.push_back(info);
        }
    }

    total_files = static_cast<int>(files_.size());
    total_lines = 0;
    total_classes = 0;
    total_functions = 0;
    for (const auto& f : files_) {
        total_lines += f.line_count;
        total_classes += f.classes.size();
        total_functions += f.functions.size();
    }
}

// ── Summary generation ────────────────────────────────────

void ProjectSummaryEngine::generate_summary(std::ostream& out) const {
    try {
        // Generate Markdown content
        out << u8"# 專案摘要報告\n\n";
        out << u8"## 概覽\n\n";
        out << u8"| 指標 | 數值 |\n";
        out << u8"|------|------|\n";
        out << u8"| 檔案數量 | " << total_files << " |\n";
        out << u8"| 總行數 | " << total_lines << " |\n";
        out << u8"| 類別/結構體 | " << total_classes << " |\n";
        out << u8"| 函式數量 | " << total_functions << " |\n\n";

        // Module breakdown
        auto modules = group_modules();
        out << u8"## 模組分組\n\n";

        for (const auto& [name, module] : modules) {
            out << u8"### " << name << "\n\n";
            out << u8"- **檔案**: " << module.files.size() << "\n";
            out << u8"- **行數**: " << module.total_lines << "\n";
            out << u8"- **類別**: " << module.class_count << "\n";
            out << u8"- **函式**: " << module.function_count << "\n\n";

            for (const auto& file : module.files) {
                out << "  - `" << file << "`\n";
            }
            out << "\n";
        }

        // Class listing
        out << u8"## 主要類別\n\n";

        for (const auto& file : files_) {
            if (!file.classes.empty()) {
                out << "### `" << file.name << "`\n\n";

                for (const auto& cls : file.classes) {
                    std::string type_str = cls.is_struct ? "struct" :
                                          (cls.is_enum ? "enum" : "class");

                    out << "**" << type_str << " ** `" << cls.name << "`\n";

                    if (!cls.base_classes.empty()) {
                        out << u8"- 繼承: ";
                        for (size_t i = 0; i < cls.base_classes.size(); ++i) {
                            if (i > 0) out << ", ";
                            out << cls.base_classes[i];
                        }
                        out << "\n";
                    }

                    out << u8"- 方法: " << cls.methods.size() << "\n";
                    out << u8"- 建構函式: " << cls.constructors.size() << "\n\n";
                }
            }
        }

        // Design patterns
        auto patterns = detect_patterns();
        if (!patterns.empty()) {
            out << u8"## 偵測到的設計模式\n\n";

            for (const auto& pattern : patterns) {
                out << "- **" << pattern.name << "**: " << pattern.description << "\n";
                out << u8"  - 檔案: ";
                for (size_t i = 0; i < pattern.files.size(); ++i) {
                    if (i > 0) out << ", ";
                    out << pattern.files[i];
                }
                out << "\n\n";
            }
        }

        // Dependencies
        auto deps = build_dependencies();
        if (!deps.empty()) {
            out << u8"## 依賴關係\n\n";

            for (const auto& dep : deps) {
                out << "- `" << dep.from_class << "` → `" << dep.to_class << "`\n";
            }
            out << "\n";
        }

    } catch (...) {
        // swallow exceptions; output may be partially written
    }
}

// ── Preview printing ──────────────────────────────────────

void ProjectSummaryEngine::print_preview() const {
    TOUT::cout << u8"=== 專案摘要 ===\n\n";

    auto modules = group_modules();

    for (const auto& [name, module] : modules) {
        TOUT::cout << u8"📁 " << name
                  << u8" — " << module.files.size() << " files"
                  << u8", " << module.total_lines << " lines"
                  << u8", " << module.class_count << " classes"
                  << u8", " << module.function_count << " functions\n";
    }

    TOUT::cout << u8"\n📊 Total: "
              << total_files << " files, "
              << total_lines << " lines, "
              << total_classes << " classes, "
              << total_functions << " functions\n";
}

// ── Convenience functions ─────────────────────────────────

// Default directories to exclude from scanning
static const std::vector<std::string> DEFAULT_EXCLUDED_DIRS = {
    // Version control
    ".git", ".hg", ".svn",
    // IDE / editor
    ".vs", ".vscode", ".idea", "out",
    // Build systems & output
    "build", "_build", "cmake", ".cmake", "obj", "bin",
    "Debug", "Release", "dist",
    // Package managers
    "vendor", "third_party", "deps", "node_modules", "packages",
    "conan", "vcpkg"
};

namespace agent {

void quick_summary(const std::string& root_dir) {
    ProjectSummaryEngine engine;
    engine.scan_directory(root_dir, DEFAULT_EXCLUDED_DIRS);
    engine.print_preview();
}

bool generate_report(const std::string& root_dir, std::ostringstream &oss) {
    ProjectSummaryEngine engine;
    engine.scan_directory(root_dir, DEFAULT_EXCLUDED_DIRS);

    // Generate into memory first
    engine.generate_summary(oss);

    // Also print preview to console
    engine.print_preview();

    return true;
}

} // namespace agent
