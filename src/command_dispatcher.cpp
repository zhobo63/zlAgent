#include "pch.h"

#include "logger.h"
#include "command_dispatcher.h"

namespace agent {

void CommandDispatcher::register_command(const std::string& name, Handler handler) {
    commands_[name] = std::move(handler);
}

bool CommandDispatcher::dispatch(const std::string& raw_input, std::string& response) {
    if (raw_input.empty() || raw_input[0] != '/') return false;

    auto tokens = tokenize(raw_input.substr(1)); // strip leading '/'
    if (tokens.empty()) return false;

    std::string cmd_name = tokens[0];
    std::vector<std::string> args(tokens.begin() + 1, tokens.end());

    auto it = commands_.find(cmd_name);
    if (it == commands_.end()) {
        response = "Unknown command: /" + cmd_name + ". Type /help for available commands.";
        LOG_ERROR("CommandDispatcher", response);
        return true; // handled - don't send to LLM
    }
    TOUT::append("\n");
    it->second(args, response);
    return true;
}

std::vector<std::string> CommandDispatcher::tokenize(const std::string& input) {
    std::vector<std::string> tokens;
    std::istringstream iss(input);
    std::string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

} // namespace agent
