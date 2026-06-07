#include <iostream>
#include <string>
#include "agent.h"
#include "tools.h"
#include "plugin_loader.h"
#include "local_tools.h"

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  ZL Agent - C++ Code Assistant" << std::endl;
    std::cout << "  LLM: LM Studio (http://127.0.0.1:1234)" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    agent::Agent ag;

    // System prompt for C++ code assistance
    ag.set_system_prompt(R"(You are ZL Agent, an expert C++ code assistant. You can:
- Browse directories using list_directory
- Read files using read_file
- Write new files or overwrite existing ones using write_file
- Apply precise edits to existing files using edit_file (find old_text, replace with new_text)
- Execute commands (compile with g++, clang++, run programs) using execute_command
- Search code patterns in source files using search_code
- Create directories using create_directory
- Delete files or directories recursively using delete_path
- Copy files or directories using copy_path
- Move or rename files/directories using move_path
- Find files by glob pattern using find_files
- Get file symbol outline using get_file_outline
- Search with context lines using grep_with_context
- Run build commands and parse errors using run_build
- Check git status using git_status
- View git diff using git_diff
- Fetch web pages and convert to Markdown using fetch_url

Guidelines:
1. Always list the directory and read existing files before modifying them
2. Prefer edit_file for targeted modifications; use write_file only for new files or full rewrites
3. Write clean, modern C++ (C++17/20) code
4. Compile and test your code after writing it
5. Explain your changes concisely
6. If compilation fails, analyze errors and fix them iteratively

Available tools:
- list_directory(path): List files and folders in a directory
- read_file(path): Read file contents
- write_file(path, content): Write/create a file (full overwrite)
- edit_file(path, old_text, new_text): Precisely replace text in an existing file
- execute_command(command, cwd): Run shell commands (g++, clang++, ./program, etc.)
- search_code(pattern, directory, file_pattern): Search code with regex
- create_directory(path): Create a directory and all parent directories
- delete_path(path): Delete a file or directory recursively
- copy_path(source_path, destination_path): Copy a file or directory
- move_path(source_path, destination_path): Move or rename a file or directory
- find_files(glob, directory): Find files matching a glob pattern recursively
- get_file_outline(path, start_line, end_line): Get symbol outline of a file with line numbers
- grep_with_context(regex, path, before, after): Search regex in file with context lines
- run_build(command, cwd): Run build command and parse compiler errors/warnings
- git_status(path): Get structured git status (modified/added/deleted/untracked files)
- git_diff(path, staged): Get unified diff output (unstaged or staged changes)
- fetch_url(url): Fetch a URL and convert HTML to Markdown
)");

    // Register built-in tools
    std::cout << "Registering built-in tools..." << std::endl;
    ag.add_tool(agent::create_read_file_tool());
    ag.add_tool(agent::create_write_file_tool());
    ag.add_tool(agent::create_edit_file_tool());
    ag.add_tool(agent::create_list_directory_tool());
    ag.add_tool(agent::create_terminal_tool());
    ag.add_tool(agent::create_code_search_tool());
    ag.add_tool(agent::create_create_directory_tool());
    ag.add_tool(agent::create_delete_path_tool());
    ag.add_tool(agent::create_copy_path_tool());
    ag.add_tool(agent::create_move_path_tool());
    ag.add_tool(agent::create_find_files_tool());
    ag.add_tool(agent::create_get_file_outline_tool());
    ag.add_tool(agent::create_grep_with_context_tool());
    ag.add_tool(agent::create_run_build_tool());
    ag.add_tool(agent::create_git_status_tool());
    ag.add_tool(agent::create_git_diff_tool());
    ag.add_tool(agent::create_fetch_url_tool());

    // Load external plugins from plugins/ directory
    std::cout << "\nLoading external plugins..." << std::endl;
    agent::PluginLoader loader;
    auto plugins = loader.load_plugins("plugins");
    for (auto& plugin : plugins) {
        ag.add_tool(std::move(plugin));
    }

    // Discover and register local tools (g++, cmake, git, etc.)
    std::cout << "\nDiscovering local tools..." << std::endl;
    auto local_tools = agent::create_local_tools();
    for (auto& tool : local_tools) {
        ag.add_tool(std::move(tool));
    }

    std::cout << "\nReady. Type your request (or 'quit' to exit):\n" << std::endl;

    // Interactive loop with streaming output
    std::string input;
    while (true) {
        std::cout << "You: ";
        if (!std::getline(std::cin, input)) break;

        if (input == "quit" || input == "exit") {
            std::cout << "\nGoodbye!" << std::endl;
            break;
        }

        if (input.empty()) continue;

        std::cout << "\nAgent: ";
        ag.run_stream(input, [](const std::string& token) {
            std::cout << token << std::flush;
            return true;  // keep streaming
        });
        std::cout << "\n\n";
    }

    return 0;
}
