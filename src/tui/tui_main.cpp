#include <zltui.h>
#include "agent.h"
#include "file_utils.h"
#include <safety_guard.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <queue>
#include <sstream>
#include <thread>

const char* dsl = u8R"(

Object Slider
{
    Name chat_area
    Rect 0 0 100 20
    Dock down|right 0 0 100 100
    DockOffset 0 0 0 -5
}

Object Slider
{
    Name files
    Rect 0 0 25 20
    Dock down|rightPane 0 0 100 100
    DockOffset 0 0 0 -5
    DrawBorder true
    Visible false
}
Object RichEdit
{
    Name file_opened
    Rect 10 0 25 20
    Dock down|right 0 0 100 100
    DockOffset 0 0 -26 -5
    DrawBorder true
    FGColor RGB(100,255,100)
    Visible false
}

Object Win
{
    Name status_bar
    Rect 0 0 0 0
    Dock top|down|right 0 100 100 100
    DockOffset 0 -4 0 -4
    Arrange content

    Object Label
    {
        Name dot
        Rect 0 0 1 0
        Text ．
    }
    Object Button
    {
        Name model
        Rect 0 0 20 0
        Text Model
        AutoSize textWidth
        BgColor unused
    }
    Clone dot
    {
    }
    Object Label
    {
        Name token_used
        Rect 0 0 10 0
        AutoSize textWidth
        Text 💸 0
    }
    Clone dot
    {
    }
    Clone token_used
    {
        Name current_iteration
        Text 🔁 0
    }
    Clone dot
    {
    }
    Object Check
    {
        Name plan
        Rect 0 0 5 0
        Text 📋
        BgColor black
    }
    Object Check
    {
        Name reflect
        Rect 0 0 5 0
        Text 🤔
        BgColor black
    }
    Object Check
    {
        Name multi_agent
        Rect 0 0 5 0
        Text 🤖
        BgColor black
    }
    Clone dot
    {
    }
    Object Combo
    {
        Name replymode
        Rect 0 0 10 0
        Text ⛔:
        BgColor unused
        Item ❌ off
        Item 🔧 exec
        Item ✏️ edit
        Item 🔄 always
    }
    Clone dot
    {
    }
    Clone token_used
    {
        Name msg_fact
        Text 💾 Msg:0 Fact:0
    }
    Clone dot
    {
    }
    Object Check
    {
        Name strict_mode
        Rect 0 0 6 0
        TextChecked 🔒
        TextUnchecked 🔓
        BgColor unused
    }
    Clone dot
    {
    }
    Clone token_used
    {
        Name white_list
        Text 📄:0
    }

}

Object Label
{
    Name input_prompt
    Rect 0 0 0 3
    Dock downPane 0 0 100 100
    FgColor RGB(235,190,95)
    Text ┃\n┃\n┃\n┃\n┃
}
Object Edit
{
    Name user_input
    Rect 1 0 100 3
    Dock downPane|right 0 0 100 100
    DrawBorder true
    BorderStyle none
    FgColor RGB(235,190,95)
    BgColor RGB(30,30,30)
}

Object Label
{
    Name user_hint
    Rect 0 0 100 0
    Autosize textWidth
    Text hint
    FGColor RGB(80,80,80)
    Visible false
}
Object Slider
{
    Name autocompelete_menu
    Rect 10 0 40 12
    Dock top|down 0 100 100 100
    DockOffset 0 -15 0 -5
    DrawBorder true
    Visible false
}

)";

const TUI::Color kTeal(76, 201, 190);
const TUI::Color kBlue(110, 170, 255);
const TUI::Color kAmber(235, 190, 95);
const TUI::Color kGreen(115, 205, 155);
const TUI::Color kRed(235, 105, 115);

struct StatusBar
{
    TUI::ButtonPtr model;
    TUI::LabelPtr token_used;
    TUI::LabelPtr current_iteration;
    TUI::CheckPtr plan;
    TUI::CheckPtr reflect;
    TUI::CheckPtr multi_agent;
    TUI::ComboPtr replymode;
    TUI::LabelPtr msg_fact;
    TUI::CheckPtr strict_mode;
    TUI::LabelPtr white_list;

    void Initialize(TUI::WinPtr status_bar) {
        model = status_bar->GetUI<TUI::Button>("model");
        token_used = status_bar->GetUI<TUI::Label>("token_used");
        current_iteration = status_bar->GetUI<TUI::Label>("current_iteration");
        plan = status_bar->GetUI<TUI::Check>("plan");
        reflect = status_bar->GetUI<TUI::Check>("reflect");
        multi_agent = status_bar->GetUI<TUI::Check>("multi_agent");
        replymode = status_bar->GetUI<TUI::Combo>("replymode");
        msg_fact = status_bar->GetUI<TUI::Label>("msg_fact");
        strict_mode = status_bar->GetUI<TUI::Check>("strict_mode");
        strict_mode->SetChecked(true);
        white_list = status_bar->GetUI<TUI::Label>("white_list");
    }

    void Update(agent::Agent& ag) {
        const int tokens_used = ag.get_tokens_used();
        const int max_tokens = ag.get_max_token();
        const int current = ag.get_current_iteration();
        const int maximum = ag.get_max_iterations();

        const double token_ratio = max_tokens > 0 ? static_cast<double>(tokens_used) / max_tokens : 0.0;
        const double iteration_ratio = maximum > 0 ? static_cast<double>(current) / maximum : 0.0;

        const TUI::Color token_color = token_ratio < 0.5 ? TUI::AnsiColor_Green
            : token_ratio < 0.8 ? TUI::AnsiColor_Yellow : TUI::AnsiColor_Red;
        const TUI::Color iteration_color = iteration_ratio >= 0.8 ? TUI::AnsiColor_Red : TUI::AnsiColor_Cyan;

        model->setText(ag.get_llm().get_model());
        model->fg_color = kBlue;

        token_used->setText(u8"💸 " + std::to_string(tokens_used));
        token_used->fg_color = token_color;

        current_iteration->setText(u8"🔁 " + std::to_string(current) +
                                    "/" + std::to_string(maximum));
        current_iteration->fg_color = iteration_color;

        plan->SetChecked(ag.task_planning_enabled());
        reflect->SetChecked(ag.self_reflection_enabled());
        multi_agent->SetChecked(ag.multi_agent_enabled());

        replymode->SetValue(static_cast<int>(ag.get_user_reply_mode()));

        const int message_count = static_cast<int>(ag.get_memory().get_messages().size());
        std::size_t facts_count = 0;
        const auto& long_term_memory = ag.get_long_term_memory();
        if (long_term_memory) {
            facts_count = long_term_memory->get_facts().size();
        }
        msg_fact->setText(u8"💾 Msg:" + std::to_string(message_count) +
                          " Fact:" + std::to_string(facts_count));
        msg_fact->fg_color = TUI::AnsiColor_Magenta;

        auto& safety_guard = agent::SafetyGuard::get_instance();
        const bool strict = safety_guard.get_strict_mode();
        strict_mode->SetChecked(strict);
        white_list->setText(u8"📄:" + std::to_string(safety_guard.path_whitelist_.size()));
        white_list->fg_color = strict ? TUI::AnsiColor_Red : TUI::AnsiColor_Green;
    }
};

struct AutoCompelete
{
    TUI::SliderPtr autocompelete_menu;
    TUI::LabelPtr user_hint;
    TUI::EditPtr user_input;

    std::vector<std::string> keywords;

    // Completion menu state (only valid when is_completion_active)
    std::vector<std::string> candidates;
    int selected = 0;
    /// Offset of the first candidate to display in the current page.
    size_t page_offset = 0;
    /// Full completion text (may be longer than the hint shown in dim color).
    std::string hint_candidates;
    bool is_completion_active = false; // whether a completion menu is currently active

    AutoCompelete(TUI::SliderPtr menu, TUI::LabelPtr hint) : autocompelete_menu(menu), user_hint(hint) {}

    // Return the path/command token immediately before the cursor.
    // `idx` is a Unicode character index, while `text` is UTF-8 encoded.
    static std::string get_prefix(const std::string& text, int idx) {
        if (idx <= 0 || text.empty())
            return {};

        size_t byte_idx = 0;
        int char_idx = 0;
        while (byte_idx < text.size() && char_idx < idx) {
            const unsigned char c = static_cast<unsigned char>(text[byte_idx]);
            if (c < 0x80) {
                byte_idx += 1;
            }
            else if ((c & 0xE0) == 0xC0 && byte_idx + 1 < text.size()) {
                byte_idx += 2;
            }
            else if ((c & 0xF0) == 0xE0 && byte_idx + 2 < text.size()) {
                byte_idx += 3;
            }
            else if ((c & 0xF8) == 0xF0 && byte_idx + 3 < text.size()) {
                byte_idx += 4;
            }
            else {
                // Treat an invalid byte as one character and keep the
                // cursor position usable for completion.
                byte_idx += 1;
            }
            ++char_idx;
        }

        size_t begin = byte_idx;
        while (begin > 0) {
            const unsigned char c = static_cast<unsigned char>(text[begin - 1]);
            if (std::isspace(c))
                break;
            --begin;
        }
        return text.substr(begin, byte_idx - begin);
    }

    static bool ci_starts_with(const std::string& str, const std::string& prefix) {
        if (str.size() < prefix.size()) return false;
        for (size_t i = 0; i < prefix.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(str[i])) !=
                std::tolower(static_cast<unsigned char>(prefix[i])))
                return false;
        return true;
    }

    static std::string normalize_path(const std::string& path) {
        std::string result = path;
        for (auto& c : result)
            if (c == '\\')
                c = '/';
        return result;
    }


    static void scan_directory(const std::filesystem::path& dir, std::vector<std::string>& entries) {
        try {
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                if (entry.is_directory()) {
                    entries.push_back(entry.path().filename().string() + "/");
                }
                else if (entry.is_regular_file()) {
                    entries.push_back(entry.path().filename().string());
                }
            }
        }
        catch (const std::filesystem::filesystem_error&) {
            // silently skip
        }
    }

    void build_candidates(const std::string &prefix) {
        // Normalize all separators to '/' so we can use a single rfind
        std::string norm = normalize_path(prefix);
        size_t last_sep = norm.rfind('/');

        if (last_sep == 0 && prefix.size() > 0) {
            // Command completion: starts with / and no path separator after it
            // e.g., "/h", "/help", "/status" -> match against keywords only
            for (const auto& kw : keywords) {
                if (ci_starts_with(kw, prefix)) {
                    candidates.push_back(kw);
                }
            }
        }
        else if (last_sep != std::string::npos) {
            // Path-aware: scan the directory before the separator
            std::filesystem::path dir_path(norm.substr(0, last_sep));
            if (!dir_path.is_absolute()) {
                dir_path = std::filesystem::current_path() / dir_path;
            }
            std::string after_sep = norm.substr(last_sep + 1);

            scan_directory(dir_path, candidates);

            if (!after_sep.empty()) {
                auto it = std::remove_if(candidates.begin(), candidates.end(),
                    [&after_sep](const std::string& e) {
                        return !ci_starts_with(e, after_sep);
                    });
                candidates.erase(it, candidates.end());
            }
        }
        else {
            // No path separator: merge keywords + current directory entries
            for (const auto& kw : keywords) {
                if (ci_starts_with(kw, prefix)) {
                    candidates.push_back(kw);
                }
            }

            std::vector<std::string> dir_entries;
            scan_directory(std::filesystem::current_path(), dir_entries);
            for (const auto& de : dir_entries) {
                if (ci_starts_with(de, prefix)) {
                    candidates.push_back(de);
                }
            }
        }

        // Remove duplicates and sort alphabetically (case-insensitive)
        std::set<std::string> seen;
        auto it = std::remove_if(candidates.begin(), candidates.end(),
            [&seen](const std::string& c) { return !seen.insert(c).second; });
        candidates.erase(it, candidates.end());

        std::sort(candidates.begin(), candidates.end(),
            [](const std::string& a, const std::string& b) {
                for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
                    char ca = std::tolower(static_cast<unsigned char>(a[i]));
                    char cb = std::tolower(static_cast<unsigned char>(b[i]));
                    if (ca != cb) return ca < cb;
                }
                return a.size() < b.size();
            });
    }

    static int utf8_char_count(const std::string& text) {
        int count = 0;
        for (unsigned char c : text) {
            if ((c & 0xC0) != 0x80)
                ++count;
        }
        return count;
    }

    void show_hint(const std::string &prefix, int hint_x, int hint_y) {
        const std::string normalized_prefix = AutoCompelete::normalize_path(prefix);
        const size_t last_sep = normalized_prefix.rfind('/');
        const int selected_index = std::max(0, std::min(
            selected, static_cast<int>(candidates.size()) - 1));
        const std::string& candidate = candidates[selected_index];
        const std::string normalized_candidate =
            AutoCompelete::normalize_path(candidate);
        const std::string completion = last_sep == std::string::npos ||
            normalized_candidate.compare(0, last_sep + 1,
                normalized_prefix, 0, last_sep + 1) == 0
            ? candidate : prefix.substr(0, last_sep + 1) + candidate;

        hint_candidates = completion;
        const std::string hint = completion.substr(prefix.size());
        if (!hint.empty()) {
            user_hint->setText(hint);
            const int hint_width = std::max(1, user_hint->GetTextSize().x);
            user_hint->local.set(hint_x, hint_y,
                hint_x + hint_width - 1, hint_y);
            user_hint->SetVisible(true);
            is_completion_active = true;
        }
        else {
            hide_hint();
        }
    }
    void hide_hint() {
        user_hint->setText("");
        user_hint->SetVisible(false);
    }

    void insert_compelete(int prefix_start, int idx) {
        if (idx < 0 || idx >= static_cast<int>(candidates.size()))
            return;
        is_completion_active = false;

        const std::string& candidate = candidates[idx];
        const int cursor_idx = user_input->cur_idx_of(user_input->cursor);
        const std::string prefix = get_prefix(user_input->text, cursor_idx);
        const std::string normalized_prefix = normalize_path(prefix);
        const size_t last_sep = normalized_prefix.rfind('/');
        const std::string normalized_candidate = normalize_path(candidate);
        const std::string completion = last_sep == std::string::npos ||
            normalized_candidate.compare(0, last_sep + 1,
                normalized_prefix, 0, last_sep + 1) == 0
            ? candidate : prefix.substr(0, last_sep + 1) + candidate;
        const int start = std::max(0, prefix_start);
        const int end = std::min(static_cast<int>(user_input->chars.size()), std::max(start, cursor_idx));
        const size_t start_byte = user_input->byte_offset_of(start);
        const size_t end_byte = user_input->byte_offset_of(end);
        user_input->text.replace(start_byte, end_byte - start_byte, completion);
        user_input->reparse();

        const int new_idx = start + utf8_char_count(completion);
        user_input->cursor = user_input->pos_of(new_idx);
        user_input->selected.unselect();
        user_input->mgr->is_dirty = true;

        autocompelete_menu->SetVisible(false);
        candidates.clear();
        hint_candidates.clear();
        if (user_input->on_edit)
            user_input->on_edit(user_input.get(), user_input->text);
    }

    void show_menu(int prefix_start, int idx) {
        constexpr size_t page_size = 9;
        autocompelete_menu->child.clear();
        if (candidates.empty()) {
            autocompelete_menu->SetVisible(false);
            return;
        }

        selected = std::max(0, std::min(selected,
            static_cast<int>(candidates.size()) - 1));
        page_offset = std::min(page_offset, candidates.size() - 1);
        if (selected < static_cast<int>(page_offset))
            page_offset = static_cast<size_t>(selected);
        else if (selected >= static_cast<int>(page_offset + page_size))
            page_offset = static_cast<size_t>(selected) - page_size + 1;

        const int width = std::max(1, autocompelete_menu->local.width() - 1);
        const size_t page_end = std::min(candidates.size(),
                                         page_offset + page_size);
        for (size_t i = page_offset; i < page_end; ++i) {
            const int y = static_cast<int>(i - page_offset);
            auto button = autocompelete_menu->Create<TUI::Button>(
                candidates[i], { 0, y, width, y });
            button->setText(std::to_string(i + 1 - page_offset) + " " + candidates[i]);
            button->text_algn = TUI::Align_Start;
            button->fg_color = i == static_cast<size_t>(selected) 
                ? TUI::AnsiColor_Bright_White : TUI::AnsiColor_Bright_Black;
            button->bg_color = i == static_cast<size_t>(selected)
                ? TUI::AnsiColor_Blue : TUI::AnsiColor_Unused;
            button->on_click = [this, prefix_start, idx, i]() {
                selected = static_cast<int>(i);
                insert_compelete(prefix_start, static_cast<int>(i));
            };
        }
        autocompelete_menu->SetVisible(true);
        autocompelete_menu->mgr->is_dirty = true;
    }

    bool OnEvent(const TUI::Event& ev) {
        if (!autocompelete_menu->is_visible || candidates.empty())
            return false;

        constexpr size_t page_size = 9;
        const int cursor_idx = user_input->cur_idx_of(user_input->cursor);
        const std::string prefix = get_prefix(user_input->text, cursor_idx);
        const int prefix_start = cursor_idx - utf8_char_count(prefix);

        auto redraw = [&]() {
            show_menu(prefix_start, cursor_idx);
            const int hint_x = user_input->clip.x + user_input->cursor.x -
                               user_input->scroll_value.x;
            const int hint_y = user_input->clip.y + user_input->cursor.y -
                               user_input->scroll_value.y;
            show_hint(prefix, hint_x, hint_y);
            user_input->mgr->is_dirty = true;
        };

        if (ev.vkey == VK_PRIOR) {
            page_offset = page_offset >= page_size
                ? page_offset - page_size : 0;
            selected = static_cast<int>(page_offset);
            redraw();
            return true;
        }
        if (ev.vkey == VK_NEXT) {
            const size_t last_page =
                ((candidates.size() - 1) / page_size) * page_size;
            page_offset = std::min(last_page, page_offset + page_size);
            selected = static_cast<int>(page_offset);
            redraw();
            return true;
        }

        if (ev.vkey == VK_UP) {
            if (selected > 0)
                --selected;
        }
        else if (ev.vkey == VK_DOWN) {
            if (selected + 1 < static_cast<int>(candidates.size()))
                ++selected;
        }
        else if (ev.key >= '1' && ev.key <= '9') {
            const size_t visible_index = page_offset +
                static_cast<size_t>(ev.key - '1');
            if (visible_index >= candidates.size() ||
                visible_index >= page_offset + page_size)
                return true;
            selected = static_cast<int>(visible_index);
            insert_compelete(prefix_start, selected);
            return true;
        }
        else if (ev.key == VK_RETURN || ev.vkey == VK_TAB) {
            insert_compelete(prefix_start, selected);
            return true;
        }
        else {
            return false;
        }

        page_offset = static_cast<size_t>(selected) / page_size * page_size;
        redraw();
        return true;
    }
};

struct History
{
    std::vector<std::string> entries;  // newest first (index 0 = most recent)
    int current_idx = -1;              // current position (-1 = not browsing)


    /// Add an entry. Removes all existing entries with the same content to avoid duplicates.
    void add(const std::string& entry) {
        if (entry.empty()) {
            reset();
            return;
        }

        for (auto it = entries.begin(); it != entries.end();) {
            if (*it == entry) {
                it = entries.erase(it);
            }
            else {
                ++it;
            }
        }

        entries.insert(entries.begin(), entry);
        reset();
    }

    /// Move to previous (older) entry. Returns true if moved.
    bool prev() {
        if (entries.empty()) {
            return false;
        }

        if (current_idx < 0) {
            current_idx = 0;
            return true;
        }

        if (current_idx + 1 >= static_cast<int>(entries.size())) {
            return false;
        }

        ++current_idx;
        return true;
    }

    /// Move to next (newer) entry. Returns true if moved.
    bool next() {
        if (current_idx < 0) {
            return false;
        }

        if (current_idx == 0) {
            reset();
            return true;
        }

        --current_idx;
        return true;
    }

    /// Get current entry or nullptr if not browsing.
    const std::string* get_current() const {
        if (current_idx < 0 || current_idx >= static_cast<int>(entries.size()))
            return nullptr;
        return &entries[current_idx];
    }

    /// Check if we are currently browsing history.
    bool is_browsing() const { return current_idx >= 0; }

    void reset() { current_idx = -1; }
};

struct FileBrowser
{
    TUI::SliderPtr files;
    std::filesystem::path rootPath;
    std::filesystem::path currentPath;
    bool show = false;

    TUI::RichEditPtr file_opened;
    std::string filename_opened = "";

    bool is_file_opened() const {
        return file_opened->is_visible;
    }

    void Show(bool _show) {
        files->SetVisible(_show);
        if (_show) {
            PopulateFiles();

            if (!filename_opened.empty()) {
                OpenFile(filename_opened);
            }
        }
        else {
            file_opened->SetVisible(false);
        }
    }

    FileBrowser(TUI::SliderPtr _files, TUI::RichEditPtr _file) :files(_files), file_opened(_file) {
        rootPath = std::filesystem::current_path();
        currentPath = std::filesystem::current_path();
    }

    bool OpenFile(const std::string& filename) {
        if (filename.empty()) {
            file_opened->SetVisible(false);
            filename_opened = filename;
            return true;
        }

        std::vector<std::string> lines;
        if (!agent::read_file_lines(filename, lines)) {
            file_opened->setText("Unable to open file: " + filename);
            file_opened->SetVisible(true);
            return false;
        }

        std::ostringstream content;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (i > 0) {
                content << '\n';
            }
            content << lines[i];
        }
        file_opened->title = filename;
        file_opened->scroll_value = { 0,0 };
        file_opened->setText("");
        TUI::SyntaxText(file_opened.get(), content.str(), filename);
        file_opened->SetVisible(true);
        filename_opened = filename;
        return true;
    }

    void AddEntry(const std::string& text, const std::filesystem::path& path, bool directory) {
        int y = 0;
        if (files->child.size() > 0) {
            y = files->child.back()->local.y2 + 1;
        }
        auto button = files->Create<TUI::Button>(path.filename().string(), { 0, y, 70, y });
        button->dock_ = { TUI::Dock_Right, {0,0,100,100}, {0,0,0,0} };
        button->setText(text);
        button->text_algn = TUI::Align_Start;
        button->on_click = [&, path, directory]() {
            if (directory) {
                currentPath = path;
                PopulateFiles();
            }
            else {
                OpenFile(path.string());
            }
            };
    }

    void PopulateFiles() {
        files->child.clear();
        files->scroll_value.y = 0;
        files->title = "." + currentPath.string().substr(rootPath.string().length());

        if (currentPath.has_parent_path() && currentPath != rootPath)
            AddEntry(u8"📁 ..", currentPath.parent_path(), true);
        try {
            for (const auto& entry : std::filesystem::directory_iterator(currentPath)) {
                AddEntry((entry.is_directory() ? u8"📁 " : u8"📄 ") +
                    entry.path().filename().string(), entry.path(), entry.is_directory());
            }
        }
        catch (const std::filesystem::filesystem_error& error) {
        }
        files->mgr->is_dirty = true;
    }
};

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

    FileBrowser filebrowser(mgr.GetUI<TUI::Slider>("files"), mgr.GetUI<TUI::RichEdit>("file_opened"));
    //filebrowser.PopulateFiles();

    auto chat_area = mgr.GetUI<TUI::Slider>("chat_area");
    auto get_chat_edit = [&]() -> TUI::RichEditPtr {
        TUI::RichEditPtr re;
        if (!chat_area->child.empty()) {
            re = std::dynamic_pointer_cast<TUI::RichEdit>(chat_area->child.back());
            if (re) {
                return re;
            }

            int y = chat_area->child.back()->local.y2 + 1;
            re = chat_area->Create<TUI::RichEdit>("", { 0, y, chat_area->clip.width() - 1, y });
            chat_area->ScrollTo(100);
        }
        else {
            re = chat_area->Create<TUI::RichEdit>("", { 0, 0, chat_area->clip.width() - 1, 0 });
        }
        re->autosize_ = TUI::Autosize_TextHeight;
        re->dock_ = { TUI::Dock_Right, {0,0,100,100}, {0,0,0,0} };
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
            lb->dock_ = { TUI::Dock_Right, {0,0,100,100}, {0,0,0,0} };
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

    TOUT::on_confirm = [&](const std::string& msg) ->bool {
        //TODO 
        // from: 
        //   multi_agent confirm_request
        //   SafetyGuard::ask_user_confirm

        return false;
        };

    AutoCompelete autocompelete(mgr.GetUI<TUI::Slider>("autocompelete_menu"), 
        mgr.GetUI<TUI::Label>("user_hint"));
    for (auto& key : TOUT::s_keywords)
        autocompelete.keywords.push_back(key);
    History history;
    StatusBar statusbar;
    statusbar.Initialize(mgr.GetUI<TUI::Win>("status_bar"));
    statusbar.Update(ag);

    auto user_input = mgr.GetUI<TUI::Edit>("user_input");
    autocompelete.user_input = user_input;

    user_input->on_edit = [&](TUI::Edit* edit, const std::string& text) {
        autocompelete.candidates.clear();
        autocompelete.selected = 0;
        autocompelete.page_offset = 0;
        autocompelete.hint_candidates.clear();
        autocompelete.is_completion_active = false;
        autocompelete.hide_hint();

        if (text.length() < 1)
            return;

        const int idx = edit->cur_idx_of(edit->cursor);
        const std::string prefix = AutoCompelete::get_prefix(text, idx);
        if (prefix.empty())
            return;

        autocompelete.build_candidates(prefix);
        if (autocompelete.candidates.empty())
            return;
        autocompelete.is_completion_active = true;
        int hint_x = user_input->clip.x + user_input->cursor.x - user_input->scroll_value.x;
        int hint_y = user_input->clip.y + user_input->cursor.y - user_input->scroll_value.y;        
        autocompelete.show_hint(prefix, hint_x, hint_y);
        };

    user_input->on_key = [&](const TUI::Event &ev) -> bool {
        if (!ev.ctrl && ev.key == VK_TAB && autocompelete.is_completion_active) {
            const int idx = user_input->cur_idx_of(user_input->cursor);
            const std::string prefix = AutoCompelete::get_prefix(user_input->text, idx);
            const int prefix_start = idx - AutoCompelete::utf8_char_count(prefix);
            if (autocompelete.candidates.size() == 1) {
                autocompelete.insert_compelete(prefix_start, 0);
            }
            else {
                autocompelete.show_menu(prefix_start, idx);
            }
            return true;
        }

        if (!ev.ctrl && ev.key == VK_RETURN) {
            if (user_input->text.empty())
                return true;
            if (user_input->text == "/quit" || user_input->text == "/exit") {
                quit = true;
                return true;
            }
            if (user_input->text == "/new") {
                chat_area->child.clear();            
            }

            if (interactive_running.exchange(true)) {
                return true;
            }

            const std::string input = user_input->text;
            history.add(input);
            TOUT::on_message({ 235,190,95 }, u8"\n┃\n┃" + input + "\n┃\n");
            user_input->setText("");

            if (interactive_thread.joinable()) {
                interactive_thread.join();
            }
            filebrowser.Show(false);

            interactive_thread = std::thread([&, input = std::move(input)] {
                std::string response;
                run_interactive(input, ag.get_dispatcher(),
                                ag.get_terminal_detector(), ag, response);
                interactive_running.store(false);
                statusbar.Update(ag);
                filebrowser.Show(filebrowser.show);
            });
            return true;
        }
        return false;
        };

    static const char* help_msg = 
        u8"  F1        Show this help\n"
        u8"  F2        Toggle file browser\n"
        u8"  F3        Insert file reference (when file is open)\n"
        u8"  F4        Quit\n"
        u8"  Esc       Close file preview\n"
        u8"  ↑ / ↓     Browse command history\n"
        u8"  /help     List available commands\n"
        u8"  /quit     Exit ZL Agent\n";

    mgr.on_key = [&](const TUI::Event& ev) -> bool {
        if (ev.vkey == VK_F4) {
            quit = true;
            return true;
        }
        else if (ev.vkey == VK_ESCAPE) {
            if (filebrowser.is_file_opened()) {
                filebrowser.OpenFile("");
            }
            else if(TOUT::on_interrupted) {
                TOUT::on_interrupted();
            }
            return true;
        }
        else if (ev.vkey == VK_F1) {
            TOUT::set_style({ 100,255,255 });   //RGB
            TOUT::on_append(
                u8"\n"
                u8"╭───────────────────────────────────╮\n"
                u8"│  ZL Agent - Shortcuts & Commands  │\n"
                u8"╰───────────────────────────────────╯\n");
            TOUT::set_style(TUI::AnsiColor_White);
            TOUT::on_append(help_msg);
        }
        else if (ev.vkey == VK_F2) {
            filebrowser.show = !filebrowser.show;
            filebrowser.Show(filebrowser.show);
        }
        else if (ev.vkey == VK_F3) {
            if (filebrowser.file_opened->is_visible &&
                !filebrowser.filename_opened.empty()) {
                std::string file_reference = filebrowser.filename_opened.substr(filebrowser.rootPath.string().length() + 1);
                const auto& selection = filebrowser.file_opened->selected;
                if (selection.has_selection() && selection.start < selection.end) {
                    const int char_count =
                        static_cast<int>(filebrowser.file_opened->chars.size());
                    const int start = std::min(char_count, std::max(0, selection.start));
                    const int end = std::min(char_count, std::max(start, selection.end));
                    int start_line = 1;
                    int end_line = 1;
                    for (int i = 0; i < start; ++i) {
                        if (filebrowser.file_opened->chars[i].ch == '\n')
                            ++start_line;
                    }
                    end_line = start_line;
                    for (int i = start; i < end; ++i) {
                        if (filebrowser.file_opened->chars[i].ch == '\n')
                            ++end_line;
                    }
                    file_reference += " " + std::to_string(start_line) +
                                      "-" + std::to_string(end_line) + " ";
                }

                int idx = user_input->cur_idx_of(user_input->cursor);
                idx = user_input->delete_selected(idx);
                idx = user_input->insert(idx, file_reference);
                user_input->selected.unselect();
                user_input->cursor = user_input->pos_of(idx);
                user_input->mgr->is_dirty = true;
                return true;
            }
        }
        else if (autocompelete.is_completion_active && autocompelete.OnEvent(ev)) {
            //used by autocompelete
            return true;
        }
        else if (ev.vkey == VK_UP) {
            if (history.prev()) {
                const auto* entry = history.get_current();
                user_input->setText(entry != nullptr ? *entry : "");
                return true;
            }
        }
        else if (ev.vkey == VK_DOWN) {
            if (history.next()) {
                const auto* entry = history.get_current();
                user_input->setText(entry != nullptr ? *entry : "");
                return true;
            }
        }
        return false;
    };

    mgr.SetNotify(user_input);

    TOUT::set_output_mode(TOUT::OutputMode_TUI);

    mgr.Update(terminal);
    TOUT::set_style({ 100,255,255 });   //RGB
    TOUT::append(u8"╭─────────────────────────────╮\n");
    TOUT::append(u8"│  ZL Agent - Code Assistant  │\n");
    TOUT::append(u8"╰─────────────────────────────╯\n");
    TOUT::set_style(TUI::AnsiColor_White);
    TOUT::append(help_msg);
    TOUT::append(u8"Ready. Type your request (or '/help' '/h' for commands):\n");
    auto& cfg = ag.get_config();
    if (cfg.terminal_commands.enabled) {
        std::cout << u8"  💡 Shell commands are auto-detected and executed directly.\n";
    }

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
    TOUT::set_output_mode(TOUT::OutputMode_cout);

    if (interactive_thread.joinable()) {
        interactive_thread.join();
    }
}
