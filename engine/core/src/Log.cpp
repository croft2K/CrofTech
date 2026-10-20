#include "croftech/core/Log.h"

#include <cstdio>
#include <mutex>
#include <vector>

namespace ct {
namespace {

struct LogState {
    std::mutex mutex;
    std::vector<std::pair<std::uint32_t, LogSink>> sinks;
    std::uint32_t nextSinkId = 1;
};

LogState& logState() {
    static LogState state;
    return state;
}

} // namespace

void writeLog(LogLevel level, std::string_view message) {
    LogState& state = logState();
    const std::lock_guard lock(state.mutex);
    std::FILE* stream = level == LogLevel::Info ? stdout : stderr;
    const char* prefix = level == LogLevel::Error     ? "error: "
                         : level == LogLevel::Warning ? "warning: "
                                                      : "";
    std::fprintf(stream, "%s%.*s\n", prefix, static_cast<int>(message.size()), message.data());
    std::fflush(stream);
    for (const auto& [id, sink] : state.sinks) {
        sink(level, message);
    }
}

std::uint32_t addLogSink(LogSink sink) {
    LogState& state = logState();
    const std::lock_guard lock(state.mutex);
    const std::uint32_t id = state.nextSinkId++;
    state.sinks.emplace_back(id, std::move(sink));
    return id;
}

void removeLogSink(std::uint32_t id) {
    LogState& state = logState();
    const std::lock_guard lock(state.mutex);
    std::erase_if(state.sinks, [id](const auto& entry) { return entry.first == id; });
}

} // namespace ct
