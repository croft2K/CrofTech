#pragma once

#include <format>
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

} // namespace ct
