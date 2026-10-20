#include "croftech/client/SimThread.h"

#include "croftech/core/Profile.h"
#include "croftech/platform/PreciseSleeper.h"

namespace ct {
namespace {

constexpr auto kTickDuration = std::chrono::duration_cast<SimThread::Clock::duration>(
    std::chrono::nanoseconds(1'000'000'000 / kTickRate));
// Further behind than this (a debugger break, a laptop waking) and the simulation resumes from now
// rather than fast-forwarding through the missed ticks.
constexpr auto kMaxCatchUp = std::chrono::milliseconds(250);

} // namespace

SimThread::SimThread(const CollisionWorld& collision, const MovementSettings& movement,
                     const State& initial)
    : m_simulation(collision, movement), m_current(std::make_unique<State>(initial)),
      m_next(std::make_unique<State>()), m_publishedPrevious(std::make_unique<State>(initial)),
      m_publishedCurrent(std::make_unique<State>(initial)), m_publishedAt(Clock::now()) {}

SimThread::~SimThread() { stop(); }

void SimThread::start() {
    if (!m_thread.joinable()) {
        m_thread = std::jthread([this](std::stop_token stop) { run(stop); });
    }
}

void SimThread::stop() {
    if (m_thread.joinable()) {
        m_thread.request_stop();
        m_thread.join();
    }
}

void SimThread::submitInput(std::uint32_t slot, const UserCmd& cmd) {
    if (slot >= kMaxPlayers) {
        return;
    }
    const std::lock_guard lock(m_inputMutex);
    m_heldInputs.commands[slot] = cmd;
    m_pressedButtons[slot] = static_cast<std::uint16_t>(m_pressedButtons[slot] | cmd.buttons);
}

void SimThread::setMovementSettings(const MovementSettings& movement) {
    const std::lock_guard lock(m_inputMutex);
    m_pendingMovement = movement;
}

InputSet SimThread::takeInputs() {
    const std::lock_guard lock(m_inputMutex);
    if (m_pendingMovement) {
        m_simulation.setMovementSettings(*m_pendingMovement);
        m_pendingMovement.reset();
    }
    InputSet inputs = m_heldInputs;
    for (std::uint32_t slot = 0; slot < kMaxPlayers; ++slot) {
        inputs.commands[slot].buttons =
            static_cast<std::uint16_t>(inputs.commands[slot].buttons | m_pressedButtons[slot]);
        m_pressedButtons[slot] = 0;
    }
    return inputs;
}

void SimThread::run(std::stop_token stop) {
    PreciseSleeper sleeper;
    Clock::time_point nextTick = Clock::now() + kTickDuration;
    while (!stop.stop_requested()) {
        sleeper.sleepUntil(nextTick);
        const Clock::time_point now = Clock::now();
        if (now - nextTick > kMaxCatchUp) {
            nextTick = now;
        }
        nextTick += kTickDuration;

        const InputSet inputs = takeInputs();
        {
            CT_PROFILE_ZONE_NAMED("tick");
            const Clock::time_point stepStart = Clock::now();
            m_simulation.step(*m_current, inputs, *m_next);
            std::swap(m_current, m_next);
            m_lastStepMilliseconds.store(
                std::chrono::duration<float, std::milli>(Clock::now() - stepStart).count());
        }
        {
            const std::lock_guard lock(m_publishMutex);
            std::swap(m_publishedPrevious, m_publishedCurrent);
            *m_publishedCurrent = *m_current;
            m_publishedAt = Clock::now();
        }
        CT_PROFILE_FRAME_NAMED("sim");
    }
}

} // namespace ct
