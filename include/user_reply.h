#pragma once

#include <string>

namespace agent {

/**
 * User Reply Mode — controls when the Agent pauses for user input during
 * the reasoning loop.
 */
enum class UserReplyMode {
    Off,        // No pause, fully automatic (default)
    OnError,    // Pause only when a tool execution fails
    Always      // Pause before every tool call
};

/**
 * Action taken by the user during an intervention prompt.
 */
enum class ReplyAction {
    Continue,   // Proceed with current parameters
    Skip,       // Skip this tool call
    Abort,      // Terminate the reasoning loop
    Edit,       // Modify arguments and execute
    Custom      // Inject custom message into conversation
};

/**
 * Result of a user reply prompt.
 */
struct UserReplyResult {
    ReplyAction action = ReplyAction::Continue;
    std::string modified_args;   // Only valid when action == Edit
    std::string custom_message;  // Only valid when action == Custom
};

/**
 * Prompt the user for input during Agent reasoning loop.
 * Displays tool name, arguments, and optional error message, then waits
 * for the user to decide how to proceed.
 *
 * @param tool_name    Name of the tool about to be executed (or that failed).
 * @param json_args    JSON argument string for the tool call.
 * @param error_message Error message if the tool already failed; empty on success.
 * @return UserReplyResult describing the user's choice.
 */
UserReplyResult prompt_user_reply(
    const std::string& tool_name,
    const std::string& json_args,
    const std::string& error_message = "");

/**
 * Parse a reply mode string from config (case-insensitive).
 * Returns UserReplyMode::Off on unrecognized input.
 */
UserReplyMode parse_reply_mode(const std::string& value);

/**
 * Convert a UserReplyMode to its string representation for display / INI.
 */
const char* reply_mode_to_string(UserReplyMode mode);

} // namespace agent
