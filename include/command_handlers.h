#pragma once

#include "command_dispatcher.h"

namespace agent {

class Agent;
class SkillRegistry;
class RAGManager;
class LongTermMemory;

/**
 * Registers all built-in CLI slash-commands with a CommandDispatcher.
 * Each handler captures the necessary global state via lambdas.
 */
void register_command_handlers(
    CommandDispatcher& dispatcher,
    Agent* ag,
    SkillRegistry* skill_registry,
    RAGManager* rag_manager,
    LongTermMemory* long_term_memory);

/**
 * Registers the /reply-mode CLI command with a CommandDispatcher.
 */
void register_reply_mode_command(CommandDispatcher& dispatcher, Agent* ag);

} // namespace agent
