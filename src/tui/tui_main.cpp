#include <zltui.h>
#include "agent.h"

void tui_main(agent::Agent& ag)
{
	TUI::Terminal terminal;
	TUI::Mgr mgr;

    bool quit = false;

    while (!quit) {
        if (mgr.Update(terminal)) {
            auto& buffer = terminal.GetDrawBuffer();
            buffer.clear();
            mgr.Paint(buffer);
            terminal.Render();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

}
