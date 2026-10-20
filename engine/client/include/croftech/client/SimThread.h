#pragma once

#include "croftech/sim/Simulation.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace ct {

class CollisionWorld;

// Runs the simulation on its own thread at exactly kTickRate, independent of the frame rate.
// The main thread submits input and reads the two latest states to interpolate between them; the
// renderer never touches the live simulation state.
class SimThread {
public:
    using Clock = std::chrono::steady_clock;

    SimThread(const CollisionWorld& collision, const MovementSettings& movement,
              const State& initial);
    ~SimThread();
    SimThread(const SimThread&) = delete;
    SimThread& operator=(const SimThread&) = delete;

    void start();
    void stop();

    // Sets the command for a slot. Buttons pressed since the last tick are kept even if released
    // before it, so a quick tap is never missed.
    void submitInput(std::uint32_t slot, const UserCmd& cmd);

    // Applied between ticks.
    void setMovementSettings(const MovementSettings& movement);

    // Calls fn(previous, current, currentPublishedAt) with the two latest ticks. Keep fn short: the
    // simulation thread waits for it before publishing the next tick.
    template <typename Fn>
    void readStates(Fn&& fn) const {
        const std::lock_guard lock(m_publishMutex);
        fn(*m_publishedPrevious, *m_publishedCurrent, m_publishedAt);
    }

    float lastStepMilliseconds() const { return m_lastStepMilliseconds.load(); }

private:
    void run(std::stop_token stop);
    InputSet takeInputs();

    Simulation m_simulation;
    std::unique_ptr<State> m_current;
    std::unique_ptr<State> m_next;

    mutable std::mutex m_publishMutex;
    std::unique_ptr<State> m_publishedPrevious;
    std::unique_ptr<State> m_publishedCurrent;
    Clock::time_point m_publishedAt;

    std::mutex m_inputMutex;
    InputSet m_heldInputs;
    std::array<std::uint16_t, kMaxPlayers> m_pressedButtons{};
    std::optional<MovementSettings> m_pendingMovement;

    std::atomic<float> m_lastStepMilliseconds{0.0f};
    std::jthread m_thread;
};

} // namespace ct
