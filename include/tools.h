#pragma once

#include "tool.h"

namespace agent {

// Tool factory functions - create instances of each tool
ToolPtr create_read_file_tool();
ToolPtr create_read_file_lines_tool();
ToolPtr create_write_file_tool();
ToolPtr create_append_file_tool();
ToolPtr create_insert_file_content_tool();
ToolPtr create_edit_file_tool();
ToolPtr create_list_directory_tool();
ToolPtr create_terminal_tool();
ToolPtr create_code_search_tool();

// Filesystem tools
ToolPtr create_create_directory_tool();
ToolPtr create_delete_path_tool();
ToolPtr create_copy_path_tool();
ToolPtr create_move_path_tool();
ToolPtr create_find_files_tool();
ToolPtr create_get_file_outline_tool();
ToolPtr create_grep_with_context_tool();
ToolPtr create_run_build_tool();
ToolPtr create_git_status_tool();
ToolPtr create_git_diff_tool();
ToolPtr create_fetch_url_tool();

// Batch file tools (read/delete/write multiple files)
ToolPtr create_read_files_tool();
ToolPtr create_delete_files_tool();
ToolPtr create_write_files_tool();
ToolPtr create_edit_files_tool();

// Project overview tool
ToolPtr create_project_overview_tool();

// Skill tools
ToolPtr create_get_skill_tool();
ToolPtr create_create_skill_tool();
ToolPtr create_delete_skill_tool();
ToolPtr create_reload_skills_tool();

// RAG tool
ToolPtr create_search_knowledge_base_tool();

// Long-term memory tools
ToolPtr create_search_memories_tool();
ToolPtr create_recall_facts_tool();

} // namespace agent
