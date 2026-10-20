#pragma once

#include <chrono>

namespace ct {

// Sleeps until a deadline with sub-millisecond accuracy, for fixed-rate loops. On Windows a plain
// sleep can overshoot by a whole scheduler quantum (up to ~15 ms), which would wreck a 7.8 ms tick;
// this uses a high-resolution waitable timer and yields for the final stretch.
// Each thread that sleeps should own its own instance.
class PreciseSleeper {
public:
    PreciseSleeper();
    ~PreciseSleeper();
    PreciseSleeper(const PreciseSleeper&) = delete;
    PreciseSleeper& operator=(const PreciseSleeper&) = delete;

    void sleepUntil(std::chrono::steady_clock::time_point deadline);

private:
    void* m_timer = nullptr;
};

} // namespace ct
