#pragma once

#include "croftech/net/Transport.h"

#include <cstdint>
#include <mutex>
#include <random>
#include <vector>

namespace ct {

// What the link does to packets, each way: a fixed delay, a random extra delay of up to jitter
// (which reorders packets, as real networks do), and a chance of losing each one. Round-trip time
// is twice the latency.
struct LinkConditions {
    float latencyMilliseconds = 0.0f;
    float jitterMilliseconds = 0.0f;
    float lossPercent = 0.0f;

    friend bool operator==(const LinkConditions&, const LinkConditions&) = default;
};

struct LinkStats {
    std::uint64_t sent = 0;
    std::uint64_t lost = 0;
    std::uint64_t delivered = 0;
};

// Two transports joined in memory, for a server and client in one process: single-player and
// play-in-editor. The conditions imitate a real network (the network conditioner), so games can
// be tried at 100 ms and more long before there are sockets. Thread-safe: the server and client
// ends may be used from different threads.
class LoopbackLink {
public:
    explicit LoopbackLink(LinkConditions conditions = {}, std::uint64_t seed = 1);
    LoopbackLink(const LoopbackLink&) = delete;
    LoopbackLink& operator=(const LoopbackLink&) = delete;

    Transport& clientEnd() { return m_ends[0]; }
    Transport& serverEnd() { return m_ends[1]; }

    // Applies to packets sent from now on.
    void setConditions(const LinkConditions& conditions);
    LinkConditions conditions() const;
    LinkStats stats() const;

private:
    struct InFlight {
        Transport::Clock::time_point arrival;
        std::uint64_t order = 0; // ties arrive in sending order
        std::vector<std::byte> bytes;
    };

    class End : public Transport {
    public:
        End(LoopbackLink& link, std::size_t index) : m_link(link), m_index(index) {}
        void send(std::span<const std::byte> packet, Clock::time_point now) override;
        bool receive(std::vector<std::byte>& packet, Clock::time_point now) override;

    private:
        LoopbackLink& m_link;
        std::size_t m_index;
    };

    void send(std::size_t to, std::span<const std::byte> packet, Transport::Clock::time_point now);
    bool receive(std::size_t at, std::vector<std::byte>& packet, Transport::Clock::time_point now);

    mutable std::mutex m_mutex;
    LinkConditions m_conditions;
    std::mt19937_64 m_random;
    std::uint64_t m_nextOrder = 0;
    LinkStats m_stats;
    std::vector<InFlight> m_inFlight[2]; // packets on their way to end 0 (client) and 1 (server)
    End m_ends[2];
};

} // namespace ct
