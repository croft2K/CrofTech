#pragma once

#include "croftech/core/Log.h"

#include <array>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

struct ImGuiInputTextCallbackData;

namespace ct {

class Console;

// The drop-down developer console: shows the log and runs what is typed into it through a
// ct::Console. Tab completes names; Up and Down recall earlier lines.
class ConsoleWindow {
public:
    explicit ConsoleWindow(Console& console); // also adds the 'clear' command
    ~ConsoleWindow();
    ConsoleWindow(const ConsoleWindow&) = delete;
    ConsoleWindow& operator=(const ConsoleWindow&) = delete;

    bool isOpen() const { return m_open; }
    void setOpen(bool open);

    // Call between DevUi::beginFrame() and endFrame().
    void draw();

private:
    struct Line {
        LogLevel level;
        std::string text;
    };

    void submit(const std::string& line);
    int onTextEdit(ImGuiInputTextCallbackData& data);

    Console& m_console;
    std::uint32_t m_logSink = 0;

    std::mutex m_linesMutex; // the log sink may be called from any thread
    std::deque<Line> m_lines;
    bool m_newLines = false;

    std::array<char, 512> m_input{};
    std::vector<std::string> m_history;
    int m_historyPosition = -1; // -1 = editing a new line
    bool m_open = false;
    bool m_focusInput = false;
};

} // namespace ct
