#include "gui_theme.hpp"
#include "output_path.hpp"
#include "process.hpp"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Preferences.H>
#include <FL/Fl_Progress.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Window.H>
#include <FL/filename.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cstring>
#include <deque>
#include <filesystem>
#include <sstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

std::string path_text(const std::filesystem::path& path)
{
    const auto bytes = path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::filesystem::path path_from_utf8(const std::string_view text)
{
    std::u8string bytes(text.size(), u8'\0');
    std::memcpy(bytes.data(), text.data(), text.size());
    return std::filesystem::path(bytes);
}

std::filesystem::path executable_directory()
{
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        throw std::runtime_error("Could not locate Vibescriber GUI.");
    }
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
#else
    return std::filesystem::read_symlink("/proc/self/exe").parent_path();
#endif
}

std::string file_uri(const std::filesystem::path& path)
{
    std::string bytes = path_text(std::filesystem::absolute(path));
#ifdef _WIN32
    std::replace(bytes.begin(), bytes.end(), '\\', '/');
#endif
    const char* hex = "0123456789ABCDEF";
    std::string uri = "file://";
#ifdef _WIN32
    uri.push_back('/');
#endif
    for (const unsigned char character : bytes) {
        if ((character >= 'A' && character <= 'Z')
            || (character >= 'a' && character <= 'z')
            || (character >= '0' && character <= '9')
            || character == '-' || character == '_' || character == '.'
            || character == '~' || character == '/' || character == ':') {
            uri.push_back(static_cast<char>(character));
        } else {
            uri.push_back('%');
            uri.push_back(hex[character >> 4U]);
            uri.push_back(hex[character & 15U]);
        }
    }
    return uri;
}

int percentage_in(const std::string& line)
{
    const auto marker = line.rfind('%');
    if (marker == std::string::npos) return -1;
    auto first = marker;
    while (first > 0 && line[first - 1] >= '0' && line[first - 1] <= '9') --first;
    if (first == marker || marker - first > 3) return -1;
    const int value = std::stoi(line.substr(first, marker - first));
    return value >= 0 && value <= 100 ? value : -1;
}

class Gui
{
public:
    Gui()
        : preferences_(Fl_Preferences::USER_L, "Vibescriber", "vibescriber-gui")
    {
        int stored_theme = 0;
        preferences_.get("theme", stored_theme, 0);
        if (stored_theme < 0 || stored_theme > 2) stored_theme = 0;
        theme_ = static_cast<vibescriber::ThemeMode>(stored_theme);

        const int initial_width = std::min(720, std::max(450, Fl::w() - 80));
        const int initial_height = std::min(500, std::max(300, Fl::h() - 80));
        window_ = std::make_unique<Fl_Window>(initial_width, initial_height,
                                              "Vibescriber");
        window_->begin();
        scroll_ = new Fl_Scroll(0, 0, initial_width, initial_height);
        scroll_->type(Fl_Scroll::BOTH);
        scroll_->begin();
        content_ = new Fl_Group(0, 0, 704, 500);
        content_->box(FL_FLAT_BOX);
        content_->begin();
        title_ = new Fl_Box(24, 18, 672, 38, "Vibescriber");
        title_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        title_->labelfont(FL_BOLD);
        title_->labelsize(24);
        subtitle_ = new Fl_Box(24, 56, 672, 26,
            "Transcribe recordings locally on your computer");
        subtitle_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        subtitle_->labelsize(14);

        file_label_ = new Fl_Box(24, 96, 672, 24, "Audio or video file");
        file_label_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        input_ = new Fl_Input(24, 122, 560, 36);
        input_->tooltip("Choose an audio or video recording");
        input_->when(FL_WHEN_CHANGED);
        input_->callback([](Fl_Widget*, void* data) {
            static_cast<Gui*>(data)->suggest_output();
        }, this);
        browse_ = new Fl_Button(594, 122, 102, 36, "Browse...");
        browse_->callback([](Fl_Widget*, void* data) {
            static_cast<Gui*>(data)->browse();
        }, this);

        output_label_ = new Fl_Box(24, 174, 672, 22, "Output transcript");
        output_label_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        output_input_ = new Fl_Input(24, 200, 560, 36);
        output_input_->tooltip("Existing files are kept; a numbered suffix is added");
        output_input_->when(FL_WHEN_CHANGED);
        output_input_->callback([](Fl_Widget*, void* data) {
            static_cast<Gui*>(data)->output_auto_ = false;
        }, this);
        output_browse_ = new Fl_Button(594, 200, 102, 36, "Browse...");
        output_browse_->callback([](Fl_Widget*, void* data) {
            static_cast<Gui*>(data)->browse_output();
        }, this);

        model_label_ = new Fl_Box(24, 250, 250, 22, "Transcription model");
        model_label_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        model_ = new Fl_Choice(24, 276, 270, 36);
        model_->add("Small + speaker turns|Medium");
        model_->value(0);
        model_->tooltip("Medium gives a fuller transcript but has no speaker turns");

        cpu_label_ = new Fl_Box(312, 250, 180, 22, "CPU use");
        cpu_label_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        cpu_ = new Fl_Choice(312, 276, 180, 36);
        cpu_->add("Balanced|Slow|Fast");
        cpu_->value(0);

        theme_label_ = new Fl_Box(510, 250, 186, 22, "Appearance");
        theme_label_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        theme_choice_ = new Fl_Choice(510, 276, 186, 36);
        theme_choice_->add("Auto|Light|Dark");
        theme_choice_->value(stored_theme);
        theme_choice_->callback([](Fl_Widget*, void* data) {
            static_cast<Gui*>(data)->change_theme();
        }, this);

        timestamps_ = new Fl_Check_Button(24, 327, 240, 30,
                                          "Include timestamps");
        start_ = new Fl_Button(24, 365, 160, 40, "Transcribe");
        start_->callback([](Fl_Widget*, void* data) {
            static_cast<Gui*>(data)->start();
        }, this);
        open_ = new Fl_Button(198, 365, 160, 40, "Open transcript");
        open_->deactivate();
        open_->callback([](Fl_Widget*, void* data) {
            static_cast<Gui*>(data)->open_transcript();
        }, this);

        progress_ = new Fl_Progress(24, 427, 672, 22);
        progress_->minimum(0);
        progress_->maximum(100);
        progress_->value(0);
        status_ = new Fl_Box(24, 458, 672, 30, "Choose a recording to begin.");
        status_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        content_->end();
        scroll_->end();
        window_->end();
        window_->resizable(scroll_);
        window_->size_range(450, 300);
        window_->callback([](Fl_Widget*, void* data) {
            auto* gui = static_cast<Gui*>(data);
            if (gui->busy_) {
                gui->set_status("Transcription is running. Wait for it to finish.");
            } else {
                gui->window_->hide();
            }
        }, this);

        apply_theme();
        Fl::add_timeout(5.0, poll_theme_callback, this);
    }

    ~Gui()
    {
        Fl::remove_timeout(poll_theme_callback, this);
        if (worker_.joinable()) worker_.join();
    }

    void show()
    {
        window_->show();
        theme_applied_ = false;
        apply_theme();
    }

private:
    struct Event
    {
        std::string line;
        int exit_code = 0;
        bool finished = false;
    };

    static void wake(void* data)
    {
        static_cast<Gui*>(data)->drain();
    }

    static void poll_theme_callback(void* data)
    {
        static_cast<Gui*>(data)->poll_theme();
    }

    void post(Event event)
    {
        bool notify = false;
        {
            std::lock_guard lock(events_mutex_);
            events_.push_back(std::move(event));
            if (!awake_pending_) {
                awake_pending_ = true;
                notify = true;
            }
        }
        if (notify) Fl::awake(wake, this);
    }

    void drain()
    {
        std::deque<Event> ready;
        {
            std::lock_guard lock(events_mutex_);
            ready.swap(events_);
            awake_pending_ = false;
        }
        for (const auto& event : ready) {
            if (event.finished) {
                finish(event.exit_code);
            } else {
                process_line(event.line);
            }
        }
    }

    void set_status(const std::string& value)
    {
        status_->copy_label(value.c_str());
        status_->copy_tooltip(value.c_str());
        status_->redraw();
    }

    void suggest_output()
    {
        const std::string_view selected(input_->value());
        if (selected.empty()) {
            output_input_->value("");
            output_auto_ = true;
            return;
        }
        if (!output_auto_ && output_input_->value()[0] != '\0') return;
        try {
            const auto suggested = vibescriber::default_output_path(
                path_from_utf8(selected));
            output_input_->value(path_text(suggested).c_str());
            output_auto_ = true;
        } catch (const std::exception&) {
            output_input_->value("");
        }
    }

    void browse()
    {
        Fl_Native_File_Chooser chooser(Fl_Native_File_Chooser::BROWSE_FILE);
        chooser.title("Choose a recording");
        chooser.filter("Audio and video\t*.{mp3,mp4,m4a,wav,flac,ogg,opus,mov,mkv}\nAll files\t*");
        if (chooser.show() == 0 && chooser.filename()) {
            input_->value(chooser.filename());
            output_auto_ = true;
            suggest_output();
            set_status("Ready to transcribe.");
        }
    }

    void browse_output()
    {
        Fl_Native_File_Chooser chooser(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
        chooser.title("Choose transcript location");
        chooser.filter("Text transcript\t*.txt\nAll files\t*");
        if (output_input_->value()[0] != '\0') {
            chooser.preset_file(output_input_->value());
        }
        if (chooser.show() == 0 && chooser.filename()) {
            output_input_->value(chooser.filename());
            output_auto_ = false;
        }
    }

    void change_theme()
    {
        theme_ = static_cast<vibescriber::ThemeMode>(theme_choice_->value());
        preferences_.set("theme", static_cast<int>(theme_));
        preferences_.flush();
        apply_theme();
    }

    void poll_theme()
    {
        if (theme_ == vibescriber::ThemeMode::automatic) apply_theme();
        Fl::repeat_timeout(5.0, poll_theme_callback, this);
    }

    void apply_theme()
    {
        const bool dark = theme_ == vibescriber::ThemeMode::dark
                          || (theme_ == vibescriber::ThemeMode::automatic
                              && vibescriber::system_prefers_dark());
        if (theme_applied_ && dark == dark_) return;
        dark_ = dark;
        theme_applied_ = true;
        const Fl_Color background = dark ? fl_rgb_color(31, 34, 39)
                                         : fl_rgb_color(245, 247, 249);
        const Fl_Color surface = dark ? fl_rgb_color(47, 51, 57)
                                      : fl_rgb_color(255, 255, 255);
        const Fl_Color text = dark ? fl_rgb_color(238, 241, 245)
                                   : fl_rgb_color(31, 38, 47);
        const Fl_Color muted = dark ? fl_rgb_color(177, 185, 196)
                                    : fl_rgb_color(89, 99, 111);
        const Fl_Color accent = dark ? fl_rgb_color(90, 157, 244)
                                     : fl_rgb_color(34, 105, 202);
        Fl::background(dark ? 31 : 245, dark ? 34 : 247, dark ? 39 : 249);
        Fl::background2(dark ? 47 : 255, dark ? 51 : 255, dark ? 57 : 255);
        Fl::foreground(dark ? 238 : 31, dark ? 241 : 38, dark ? 245 : 47);
        Fl::set_color(FL_SELECTION_COLOR, accent);
        window_->color(background);
        scroll_->color(background);
        content_->color(background);
        for (Fl_Box* label : {title_, subtitle_, file_label_, output_label_,
                              model_label_, cpu_label_, theme_label_, status_}) {
            label->color(background);
            label->labelcolor(label == subtitle_ ? muted : text);
        }
        for (Fl_Button* button : {browse_, output_browse_, start_, open_}) {
            button->color(button == start_ ? accent : surface);
            button->labelcolor(button == start_ ? FL_WHITE : text);
            button->selection_color(accent);
        }
        for (Fl_Input* field : {input_, output_input_}) {
            field->color(surface);
            field->textcolor(text);
            field->cursor_color(text);
        }
        for (Fl_Choice* choice : {model_, cpu_, theme_choice_}) {
            choice->color(surface);
            choice->textcolor(text);
            choice->labelcolor(text);
            choice->selection_color(accent);
        }
        timestamps_->color(background);
        timestamps_->labelcolor(text);
        timestamps_->selection_color(accent);
        progress_->color(surface);
        progress_->selection_color(accent);
        progress_->labelcolor(text);
        window_->redraw();
    }

    void start()
    {
        std::filesystem::path input;
        std::filesystem::path requested_output;
        try {
            input = path_from_utf8(input_->value());
            requested_output = path_from_utf8(output_input_->value());
        } catch (const std::exception& error) {
            set_status(std::string("Invalid file path: ") + error.what());
            return;
        }
        std::error_code input_error;
        if (!std::filesystem::is_regular_file(input, input_error)) {
            set_status("Choose an existing audio or video file.");
            return;
        }
        if (requested_output.empty()) {
            set_status("Choose an output transcript path.");
            return;
        }
        std::filesystem::path cli;
        try {
#ifdef _WIN32
            cli = executable_directory() / "vibescriber.exe";
#else
            cli = executable_directory() / "vibescriber";
#endif
        } catch (const std::exception& error) {
            set_status(error.what());
            return;
        }
        if (!std::filesystem::is_regular_file(cli)) {
            set_status("The vibescriber CLI is missing beside the GUI.");
            return;
        }
        if (worker_.joinable()) worker_.join();
        std::vector<std::filesystem::path> arguments;
        if (model_->value() == 1) {
            arguments.emplace_back("--model");
            arguments.emplace_back("medium.en");
        }
        if (cpu_->value() == 1) arguments.emplace_back("--slow");
        if (cpu_->value() == 2) arguments.emplace_back("--fast");
        if (timestamps_->value()) arguments.emplace_back("--timestamps");
        arguments.emplace_back("--output");
        arguments.push_back(requested_output);
        arguments.emplace_back("--");
        arguments.push_back(input);

        busy_ = true;
        input_->deactivate();
        browse_->deactivate();
        output_input_->deactivate();
        output_browse_->deactivate();
        model_->deactivate();
        cpu_->deactivate();
        timestamps_->deactivate();
        start_->deactivate();
        open_->deactivate();
        completed_output_.clear();
        last_error_.clear();
        progress_->value(0);
        set_status("Starting transcription...");
        worker_ = std::thread([this, cli, arguments = std::move(arguments)] {
            std::string pending;
            try {
                const int result = vibescriber::run_process_capture(cli, arguments,
                    [this, &pending](const std::string_view chunk) {
                        for (const char character : chunk) {
                            if (character == '\r' || character == '\n') {
                                if (!pending.empty()) {
                                    post({std::move(pending), 0, false});
                                    pending.clear();
                                }
                            } else {
                                pending.push_back(character);
                            }
                        }
                    });
                if (!pending.empty()) post({std::move(pending), 0, false});
                post({{}, result, true});
            } catch (const std::exception& error) {
                post({std::string("error: ") + error.what(), 0, false});
                post({{}, 1, true});
            }
        });
    }

    void process_line(const std::string& line)
    {
        if (line.empty()) return;
        if (line.rfind("Transcript written to ", 0) == 0) {
            std::istringstream stream(line.substr(22));
            std::filesystem::path reported;
            if (stream >> reported) completed_output_ = reported;
        }
        if (line.rfind("error: ", 0) == 0) last_error_ = line;
        const int percentage = percentage_in(line);
        if (percentage >= 0) progress_->value(percentage);
        else if (line.ends_with("...")) progress_->value(0);
        if (line.rfind("Output: ", 0) != 0 && line.rfind("Input: ", 0) != 0) {
            set_status(line);
        }
    }

    void finish(const int exit_code)
    {
        busy_ = false;
        input_->activate();
        browse_->activate();
        output_input_->activate();
        output_browse_->activate();
        model_->activate();
        cpu_->activate();
        timestamps_->activate();
        start_->activate();
        if (exit_code == 0) {
            progress_->value(100);
            if (!completed_output_.empty()
                && std::filesystem::is_regular_file(completed_output_)) {
                open_->activate();
                set_status("Saved transcript to " + path_text(completed_output_));
            } else {
                set_status("Transcription complete.");
            }
        } else {
            set_status(last_error_.empty() ? "Transcription failed." : last_error_);
        }
    }

    void open_transcript()
    {
        if (completed_output_.empty()) return;
        const auto uri = file_uri(completed_output_);
        char message[512]{};
        if (!fl_open_uri(uri.c_str(), message, sizeof(message))) {
            set_status(std::string("Could not open transcript: ") + message);
        }
    }

    Fl_Preferences preferences_;
    std::unique_ptr<Fl_Window> window_;
    Fl_Scroll* scroll_ = nullptr;
    Fl_Group* content_ = nullptr;
    Fl_Box *title_ = nullptr, *subtitle_ = nullptr, *file_label_ = nullptr;
    Fl_Box *output_label_ = nullptr;
    Fl_Box *model_label_ = nullptr, *cpu_label_ = nullptr, *theme_label_ = nullptr;
    Fl_Box* status_ = nullptr;
    Fl_Input *input_ = nullptr, *output_input_ = nullptr;
    Fl_Button *browse_ = nullptr, *output_browse_ = nullptr;
    Fl_Button *start_ = nullptr, *open_ = nullptr;
    Fl_Choice *model_ = nullptr, *cpu_ = nullptr, *theme_choice_ = nullptr;
    Fl_Check_Button* timestamps_ = nullptr;
    Fl_Progress* progress_ = nullptr;
    vibescriber::ThemeMode theme_ = vibescriber::ThemeMode::automatic;
    bool theme_applied_ = false;
    bool dark_ = false;
    bool busy_ = false;
    bool output_auto_ = true;
    std::filesystem::path completed_output_;
    std::string last_error_;
    std::thread worker_;
    std::mutex events_mutex_;
    std::deque<Event> events_;
    bool awake_pending_ = false;
};

} // namespace

int main()
{
    if (Fl::lock() != 0) return 1;
    Gui gui;
    gui.show();
    return Fl::run();
}
