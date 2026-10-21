#include "croftech/server/ServerThread.h"

#include "croftech/core/Profile.h"
#include "croftech/platform/PreciseSleeper.h"

namespace ct {
namespace {

constexpr auto kTickDuration = std::chrono::duration_cast<ServerThread::Clock::duration>(
    std::chrono::nanoseconds(1'000'000'000 / kTickRate));
// Further behind than this (a debugger break, a laptop waking) and the server resumes from now
// rather than fast-forwarding through the missed ticks.
constexpr auto kMaxCatchUp = std::chrono::milliseconds(250);

} // namespace

ServerThread::~ServerThread() { stop(); }

void ServerThread::start() {
    if (!m_thread.joinable()) {
        m_thread = std::jthread([this](std::stop_token stop) { run(stop); });
    }
}

void ServerThread::stop() {
    if (m_thread.joinable()) {
        m_thread.request_stop();
        m_thread.join();
    }
}

void ServerThread::run(std::stop_token stop) {
    PreciseSleeper sleeper;
    Clock::time_point nextTick = Clock::now() + kTickDuration;
    while (!stop.stop_requested()) {
        sleeper.sleepUntil(nextTick);
        const Clock::time_point now = Clock::now();
        if (now - nextTick > kMaxCatchUp) {
            nextTick = now;
        }
        nextTick += kTickDuration;
        {
            CT_PROFILE_ZONE_NAMED("server tick");
            const std::lock_guard lock(m_mutex);
            m_server.receive(now);
            m_server.tick(now);
            m_lastTickMilliseconds.store(
                std::chrono::duration<float, std::milli>(Clock::now() - now).count());
        }
        CT_PROFILE_FRAME_NAMED("server");
    }
}

} // namespace ct
