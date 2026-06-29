#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>

namespace agent {

/**
 * Dispatches slash-commands entered at the CLI prompt.
 * Commands start with '/' and are handled immediately without involving the LLM.
 */
class CommandDispatcher {
public:
    using Handler = std::function<void(const std::vector<std::string>& args, std::string& response)>;

    // Register a command handler. The name should NOT include the leading '/'.
    void register_command(const std::string& name, Handler handler);

    // Try to dispatch an input string that starts with '/'.
    // Returns true if a command was handled (caller should skip LLM processing).
    // If response is non-null, the handler can write its output there.
    bool dispatch(const std::string& raw_input, std::string& response);

private:
    std::map<std::string, Handler> commands_;

    // Split "command arg1 arg2" into ["command", "arg1", "arg2"].
    static std::vector<std::string> tokenize(const std::string& input);
};

} // namespace agent
