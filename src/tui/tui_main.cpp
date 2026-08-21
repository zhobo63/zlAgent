#include <zltui.h>
#include "agent.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

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

bool run_interactive(
    const std::string& input,
    agent::CommandDispatcher& dispatcher,
    agent::TerminalCommandDetector* terminal_detector,
    agent::Agent& ag,
    std::string& response);

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

    auto chat_area = mgr.GetUI<TUI::Slider>("chat_area");
    auto get_chat_edit = [&]() -> TUI::RichEditPtr {
        TUI::RichEditPtr re;
        if (!chat_area->child.empty()) {
            re = std::dynamic_pointer_cast<TUI::RichEdit>(chat_area->child.back());
            if (re) {
                return re;
            }

            int y = chat_area->child.back()->local.y2 + 1;
            re = chat_area->Create<TUI::RichEdit>(
                "", { 0, y, chat_area->clip.width() - 1, y });
            chat_area->ScrollTo(100);
        }
        else {
            re = chat_area->Create<TUI::RichEdit>(
                "", { 0, 0, chat_area->clip.width() - 1, 0 });
        }
        re->autosize_ = TUI::Autosize_TextHeight;
        re->is_scroll_y = false;
        return re;
    };

    std::mutex ui_task_mutex;
    std::queue<std::function<void()>> ui_tasks;
    auto post_ui_task = [&](std::function<void()> task) {
        std::lock_guard<std::mutex> lock(ui_task_mutex);
        ui_tasks.push(std::move(task));
    };

    std::atomic<bool> interactive_running{false};
    std::thread interactive_thread;
    bool quit = false;

    TOUT::on_message = [&](const TUI::Color color, const std::string& msg) {
        post_ui_task([&, color, msg] {
            int y = 0;
            if (!chat_area->child.empty()) {
                y = chat_area->child.back()->local.y2 + 1;
            }
            auto lb = chat_area->Create<TUI::Label>(
                "", { 0, y, chat_area->clip.width() - 1, y });
            lb->autosize_ = TUI::Autosize_TextHeight;
            lb->fg_color = color;
            lb->setText(msg);
            chat_area->ScrollTo(100);
            });
        };
    TOUT::on_append = [&](const std::string& msg) {
        const auto style = TOUT::current_style;
        post_ui_task([&, msg, style] {
            auto re = get_chat_edit();
            re->appendText(msg, style);
            });
        };
    std::string markdown;
    TOUT::on_token = [&](bool reasoning, const std::string& msg) {
        const auto style = TOUT::current_style;
        post_ui_task([&, reasoning, msg, style] {
            auto re = get_chat_edit();
            if (reasoning) {
                markdown.clear();
                re->appendText(msg, style);
            }
            else {
                markdown += msg;
                re->setText("");
                TUI::Markdown(re.get(), markdown);
            }
            chat_area->ScrollTo(100);
            });
        };

    auto user_input = mgr.GetUI<TUI::Edit>("user_input");
    user_input->on_key = [&](const TUI::Event &ev) -> bool {
        if (!ev.ctrl && ev.key == VK_RETURN) {
            if (user_input->text.empty())
                return true;
            if (user_input->text == "/quit" || user_input->text == "/exit") {
                quit = true;
                return true;
            }

            if (interactive_running.exchange(true)) {
                return true;
            }

            const std::string input = user_input->text;
            TOUT::on_message({ 235,190,95 }, u8"\n┃" + input + "\n");
            user_input->setText("");

            if (interactive_thread.joinable()) {
                interactive_thread.join();
            }

            interactive_thread = std::thread([
                &, input = std::move(input)
            ] {
                std::string response;
                run_interactive(input, ag.get_dispatcher(),
                                ag.get_terminal_detector(), ag, response);
                interactive_running.store(false);
            });
            return true;
        }
        return false;
        };
    mgr.on_key = [&](const TUI::Event& ev) -> bool {
        if (ev.vkey == VK_F4)
            quit = true;
        return false;
        };

    mgr.SetNotify(user_input);



    TOUT::set_output_mode(TOUT::OutputMode_TUI);

    while (!quit) {
        std::queue<std::function<void()>> pending_tasks;
        {
            std::lock_guard<std::mutex> lock(ui_task_mutex);
            pending_tasks.swap(ui_tasks);
        }
        while (!pending_tasks.empty()) {
            auto task = std::move(pending_tasks.front());
            pending_tasks.pop();
            task();
        }

        if (mgr.Update(terminal)) {
            auto& buffer = terminal.GetDrawBuffer();
            buffer.clear();
            mgr.Paint(buffer);
            terminal.Render();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (interactive_thread.joinable()) {
        interactive_thread.join();
    }
}
