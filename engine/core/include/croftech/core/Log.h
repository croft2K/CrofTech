#pragma once

#include <cstdint>
#include <format>
#include <functional>
#include <string_view>
#include <utility>

namespace ct {

enum class LogLevel { Info, Warning, Error };

void writeLog(LogLevel level, std::string_view message);

template <typename... Args>
void logInfo(std::format_string<Args...> format, Args&&... args) {
    writeLog(LogLevel::Info, std::format(format, std::forward<Args>(args)...));
}

template <typename... Args>
void logWarning(std::format_string<Args...> format, Args&&... args) {
    writeLog(LogLevel::Warning, std::format(format, std::forward<Args>(args)...));
}

template <typename... Args>
void logError(std::format_string<Args...> format, Args&&... args) {
    writeLog(LogLevel::Error, std::format(format, std::forward<Args>(args)...));
}

// Receives every message after it reaches the terminal, on whichever thread logged it. Sinks run
// under the log lock: keep them short and never log from one.
using LogSink = std::function<void(LogLevel level, std::string_view message)>;

// Returns an id for removeLogSink.
std::uint32_t addLogSink(LogSink sink);
void removeLogSink(std::uint32_t id);

} // namespace ct
