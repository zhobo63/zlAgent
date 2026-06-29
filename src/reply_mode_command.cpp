#include "pch.h"

#include "command_dispatcher.h"
#include "agent.h"
#include "user_reply.h"

namespace agent {

/**
 * Registers the /reply-mode CLI command with a CommandDispatcher.
 */
void register_reply_mode_command(CommandDispatcher& dispatcher, Agent* ag) {
    dispatcher.register_command("reply-mode", [ag](const std::vector<std::string>& args) {
        if (!ag) return;

        // No arguments — show current mode and usage.
        if (args.size() <= 1) {
            auto mode = ag->get_user_reply_mode();
            TUI::out("\n--- User Reply Mode ---\n"
                      "  Current mode: %s\n"
                      "\n"
                      "  Modes:\n"
                      "    off       - Fully automatic, no intervention\n"
                      "    on_error  - Pause when a tool call fails\n"
                      "    always    - Pause before every tool call\n"
                      "\n"
                      "  Usage: /reply-mode <mode>\n",
                    reply_mode_to_string(mode));
            return;
        }

        // args[0] is the command name, args[1] is the mode.
        auto new_mode = parse_reply_mode(args[1]);

        if (new_mode == ag->get_user_reply_mode()) {
            TUI::out("\n  User reply mode is already: %s\n", reply_mode_to_string(new_mode));
            return;
        }

        ag->set_user_reply_mode(new_mode);
        TUI::out("\n  User reply mode changed to: %s\n", reply_mode_to_string(new_mode));
    });
}

} // namespace agent
