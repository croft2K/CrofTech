#include "croftech/devui/ConsoleWindow.h"

#include "croftech/core/Console.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <span>
#include <string_view>

namespace ct {
namespace {

constexpr std::size_t kMaxLines = 2000;
constexpr std::size_t kMaxHistory = 100;
constexpr float kHeightFraction = 0.45f;

std::string commonPrefix(std::span<const std::string> names) {
    std::string prefix = names.front();
    for (const std::string& name : names.subspan(1)) {
        const auto mismatch = std::mismatch(prefix.begin(), prefix.end(), name.begin(), name.end());
        prefix.erase(mismatch.first, prefix.end());
    }
    return prefix;
}

ImVec4 lineColor(LogLevel level) {
    switch (level) {
    case LogLevel::Warning:
        return {1.0f, 0.8f, 0.3f, 1.0f};
    case LogLevel::Error:
        return {1.0f, 0.4f, 0.4f, 1.0f};
    case LogLevel::Info:
        break;
    }
    return ImGui::GetStyleColorVec4(ImGuiCol_Text);
}

} // namespace

ConsoleWindow::ConsoleWindow(Console& console) : m_console(console) {
    m_logSink = addLogSink([this](LogLevel level, std::string_view message) {
        const std::lock_guard lock(m_linesMutex);
        m_lines.push_back({level, std::string(message)});
        if (m_lines.size() > kMaxLines) {
            m_lines.pop_front();
        }
        m_newLines = true;
    });
    m_console.addCommand("clear", "Clear the console", [this](std::span<const std::string>) {
        const std::lock_guard lock(m_linesMutex);
        m_lines.clear();
    });
}

ConsoleWindow::~ConsoleWindow() {
    m_console.removeCommand("clear");
    removeLogSink(m_logSink);
}

void ConsoleWindow::setOpen(bool open) {
    m_open = open;
    m_focusInput = open;
}

void ConsoleWindow::draw() {
    if (!m_open) {
        return;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize({viewport->WorkSize.x, viewport->WorkSize.y * kHeightFraction});
    ImGui::SetNextWindowBgAlpha(0.92f);
    constexpr ImGuiWindowFlags kWindowFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
    if (ImGui::Begin("Console", nullptr, kWindowFlags)) {
        const float inputHeight = ImGui::GetFrameHeightWithSpacing();
        if (ImGui::BeginChild("log", {0.0f, -inputHeight}, ImGuiChildFlags_None,
                              ImGuiWindowFlags_HorizontalScrollbar)) {
            // Follow new output unless the user has scrolled up to read something.
            const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
            const std::lock_guard lock(m_linesMutex);
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(m_lines.size()));
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    const Line& line = m_lines[static_cast<std::size_t>(i)];
                    ImGui::PushStyleColor(ImGuiCol_Text, lineColor(line.level));
                    ImGui::TextUnformatted(line.text.data(), line.text.data() + line.text.size());
                    ImGui::PopStyleColor();
                }
            }
            if (m_newLines && atBottom) {
                ImGui::SetScrollHereY(1.0f);
            }
            m_newLines = false;
        }
        ImGui::EndChild();

        ImGui::SetNextItemWidth(-FLT_MIN);
        if (m_focusInput) {
            ImGui::SetKeyboardFocusHere();
            m_focusInput = false;
        }
        constexpr ImGuiInputTextFlags kInputFlags = ImGuiInputTextFlags_EnterReturnsTrue |
                                                    ImGuiInputTextFlags_CallbackCompletion |
                                                    ImGuiInputTextFlags_CallbackHistory;
        const auto callback = [](ImGuiInputTextCallbackData* data) {
            return static_cast<ConsoleWindow*>(data->UserData)->onTextEdit(*data);
        };
        if (ImGui::InputText("##input", m_input.data(), m_input.size(), kInputFlags, callback,
                             this)) {
            const std::string line = m_input.data();
            m_input[0] = '\0';
            submit(line);
            m_focusInput = true; // Enter drops focus; keep typing
        }
    }
    ImGui::End();
}

void ConsoleWindow::submit(const std::string& line) {
    m_historyPosition = -1;
    if (line.find_first_not_of(' ') == std::string::npos) {
        return;
    }
    if (m_history.empty() || m_history.back() != line) {
        m_history.push_back(line);
        if (m_history.size() > kMaxHistory) {
            m_history.erase(m_history.begin());
        }
    }
    {
        const std::lock_guard lock(m_linesMutex);
        m_newLines = true; // always show the result, even if scrolled up
    }
    logInfo("] {}", line);
    m_console.execute(line);
}

int ConsoleWindow::onTextEdit(ImGuiInputTextCallbackData& data) {
    if (data.EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
        const std::string_view text(data.Buf, static_cast<std::size_t>(data.CursorPos));
        const std::size_t start = text.find_last_of(" ;") + 1; // npos + 1 == 0
        const std::string_view word = text.substr(start);
        const std::vector<std::string> matches = m_console.complete(word);
        if (matches.empty()) {
            return 0;
        }
        std::string replacement = matches.size() == 1 ? matches[0] + " " : commonPrefix(matches);
        if (matches.size() > 1) {
            for (const std::string& match : matches) {
                const CVar* cvar = m_console.find(match);
                if (cvar != nullptr) {
                    logInfo("  {} = {}", match, cvar->text());
                } else {
                    logInfo("  {}", match);
                }
            }
        }
        data.DeleteChars(static_cast<int>(start), static_cast<int>(word.size()));
        data.InsertChars(data.CursorPos, replacement.c_str());
    } else if (data.EventFlag == ImGuiInputTextFlags_CallbackHistory && !m_history.empty()) {
        const int last = static_cast<int>(m_history.size()) - 1;
        if (data.EventKey == ImGuiKey_UpArrow) {
            m_historyPosition = m_historyPosition == -1 ? last : std::max(m_historyPosition - 1, 0);
        } else if (data.EventKey == ImGuiKey_DownArrow && m_historyPosition != -1) {
            m_historyPosition = m_historyPosition == last ? -1 : m_historyPosition + 1;
        }
        data.DeleteChars(0, data.BufTextLen);
        if (m_historyPosition != -1) {
            data.InsertChars(0, m_history[static_cast<std::size_t>(m_historyPosition)].c_str());
        }
    }
    return 0;
}

} // namespace ct
