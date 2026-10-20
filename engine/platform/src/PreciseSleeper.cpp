#include "croftech/platform/PreciseSleeper.h"

#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace ct {

using Clock = std::chrono::steady_clock;

#if defined(_WIN32)

namespace {
// The timer wakes this early; the rest is spent yielding, which is accurate to microseconds.
constexpr auto kSpinWindow = std::chrono::microseconds(500);
} // namespace

PreciseSleeper::PreciseSleeper()
    : m_timer(CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                     TIMER_ALL_ACCESS)) {}

PreciseSleeper::~PreciseSleeper() {
    if (m_timer != nullptr) {
        CloseHandle(m_timer);
    }
}

void PreciseSleeper::sleepUntil(Clock::time_point deadline) {
    for (;;) {
        const auto remaining = deadline - Clock::now();
        if (remaining <= Clock::duration::zero()) {
            return;
        }
        if (m_timer != nullptr && remaining > kSpinWindow) {
            const auto wait =
                std::chrono::duration_cast<std::chrono::nanoseconds>(remaining - kSpinWindow);
            LARGE_INTEGER due;
            due.QuadPart = -static_cast<LONGLONG>(wait.count() / 100); // relative, 100 ns units
            if (due.QuadPart < 0 &&
                SetWaitableTimerEx(m_timer, &due, 0, nullptr, nullptr, nullptr, 0)) {
                WaitForSingleObject(m_timer, INFINITE);
                continue;
            }
        }
        std::this_thread::yield();
    }
}

#else

PreciseSleeper::PreciseSleeper() = default;
PreciseSleeper::~PreciseSleeper() = default;

void PreciseSleeper::sleepUntil(Clock::time_point deadline) {
    std::this_thread::sleep_until(deadline);
}

#endif

} // namespace ct
