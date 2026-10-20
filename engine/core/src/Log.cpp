#include "croftech/core/Log.h"

#include <cstdio>
#include <mutex>

namespace ct {

void writeLog(LogLevel level, std::string_view message) {
    static std::mutex mutex;
    const std::lock_guard lock(mutex);
    std::FILE* stream = level == LogLevel::Info ? stdout : stderr;
    const char* prefix = level == LogLevel::Error     ? "error: "
                         : level == LogLevel::Warning ? "warning: "
                                                      : "";
    std::fprintf(stream, "%s%.*s\n", prefix, static_cast<int>(message.size()), message.data());
    std::fflush(stream);
}

} // namespace ct
