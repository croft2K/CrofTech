#pragma once

#include <array>

namespace ct {

struct FrameStats {
    float frameMilliseconds = 0.0f; // wall time since the previous frame
    float tickMilliseconds = 0.0f;  // cost of the latest simulation tick
    float speed = 0.0f;             // player's horizontal speed, m/s
};

// A netgraph-style corner overlay: frame rate, frame and tick cost, and speed, with recent history
// as graphs so hitches and strafe-jump speed gains are easy to see.
class StatsOverlay {
public:
    void add(const FrameStats& stats);
    // Call between DevUi::beginFrame() and endFrame().
    void draw() const;

private:
    static constexpr int kHistory = 240;

    std::array<float, kHistory> m_frameMilliseconds{};
    std::array<float, kHistory> m_speed{};
    int m_next = 0;
    int m_count = 0;
    FrameStats m_latest;
};

} // namespace ct
