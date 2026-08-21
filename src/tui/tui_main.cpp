#include <zltui.h>
#include "agent.h"

const char* dsl = u8R"(

Object Slider
{
    Name chat_area
    Rect 0 0 100 20
    Dock down|right 0 0 100 100
    DockOffset 0 0 -26 -5
}

Object Slider
{
    Name files
    Rect 0 0 25 20
    Dock down|rightPane 0 0 100 100
    DockOffset 0 0 0 -5
    DrawBorder true
}

Object Label
{
    Name input_prompt
    Rect 0 0 0 4
    Dock downPane 0 0 100 100
    FgColor RGB(235,190,95)
    Text ┃\n┃\n┃\n┃\n┃
}
Object Edit
{
    Name user_input
    Rect 1 0 100 4
    Dock downPane|right 0 0 100 100
    DrawBorder true
    BorderStyle none
    FgColor RGB(235,190,95)
    BgColor RGB(30,30,30)
}
)";

const TUI::Color kTeal(76, 201, 190);
const TUI::Color kBlue(110, 170, 255);
const TUI::Color kAmber(235, 190, 95);
const TUI::Color kGreen(115, 205, 155);
const TUI::Color kRed(235, 105, 115);

void tui_main(agent::Agent& ag)
{
	TUI::Terminal terminal;
	TUI::Mgr mgr;
    mgr.Parse(dsl);

    std::filesystem::path rootPath = std::filesystem::current_path();
    std::filesystem::path currentPath = std::filesystem::current_path();
    auto files = mgr.GetUI<TUI::Slider>("files");
    std::function<void()> populateFiles;
    populateFiles = [&]() {
        files->child.clear();
        files->scroll_value.y = 0;
        files->title = "." + currentPath.string().substr(rootPath.string().length());
        int y = 0;
        auto addEntry = [&](const std::string& text, const std::filesystem::path& path,
            bool directory) {
                auto button = files->Create<TUI::Button>(path.filename().string(), { 0, y, 70, y });
                button->dock_ = { TUI::Dock_Right, {0,0,100,100}, {0,0,0,0} };
                button->setText(text);
                button->text_algn = TUI::Align_Start;
                button->on_click = [&, path, directory]() {
                    if (directory) {
                        currentPath = path;
                        populateFiles();
                    }
                    else {
                        //TODO open file
                    }
                    };
                ++y;
            };
        if (currentPath.has_parent_path() && currentPath != rootPath)
            addEntry(u8"📁 ..", currentPath.parent_path(), true);
        try {
            for (const auto& entry : std::filesystem::directory_iterator(currentPath)) {
                addEntry((entry.is_directory() ? u8"📁 " : u8"📄 ") +
                    entry.path().filename().string(), entry.path(), entry.is_directory());
            }
        }
        catch (const std::filesystem::filesystem_error& error) {
        }
        mgr.is_dirty = true;
        };
    populateFiles();


    auto user_input = mgr.GetUI<TUI::Edit>("user_input");
    mgr.SetNotify(user_input);

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
