#pragma once

#include "isocline.h"

namespace agent {

/**
 * Registers the isocline auto-completion callback for slash commands.
 * Call this after ic_init() to enable tab completion in the CLI.
 */
void register_completion();

} // namespace agent
