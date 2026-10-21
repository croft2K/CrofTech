#pragma once

#include <chrono>
#include <cstddef>
#include <span>
#include <vector>

namespace ct {

// One end of an unreliable packet link: packets may arrive late, out of order or not at all, as
// over UDP. The game's server and client talk only through this, so the in-memory link used now
// (LoopbackLink) and real sockets (M6) are interchangeable. Times are passed in rather than read,
// so tests can drive a link with made-up clocks.
class Transport {
public:
    using Clock = std::chrono::steady_clock;

    virtual ~Transport() = default;

    virtual void send(std::span<const std::byte> packet, Clock::time_point now) = 0;
    // The next packet that has arrived by now, copied into packet (reusing its memory). False if
    // none has.
    virtual bool receive(std::vector<std::byte>& packet, Clock::time_point now) = 0;
};

} // namespace ct
