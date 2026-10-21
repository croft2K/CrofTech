#include "croftech/net/LoopbackLink.h"

#include <algorithm>
#include <cmath>

namespace ct {
namespace {

Transport::Clock::duration milliseconds(float value) {
    return std::chrono::duration_cast<Transport::Clock::duration>(
        std::chrono::duration<float, std::milli>(std::max(value, 0.0f)));
}

} // namespace

LoopbackLink::LoopbackLink(LinkConditions conditions, std::uint64_t seed)
    : m_conditions(conditions), m_random(seed), m_ends{End(*this, 0), End(*this, 1)} {}

void LoopbackLink::setConditions(const LinkConditions& conditions) {
    const std::lock_guard lock(m_mutex);
    m_conditions = conditions;
}

LinkConditions LoopbackLink::conditions() const {
    const std::lock_guard lock(m_mutex);
    return m_conditions;
}

LinkStats LoopbackLink::stats() const {
    const std::lock_guard lock(m_mutex);
    return m_stats;
}

void LoopbackLink::End::send(std::span<const std::byte> packet, Clock::time_point now) {
    m_link.send(1 - m_index, packet, now);
}

bool LoopbackLink::End::receive(std::vector<std::byte>& packet, Clock::time_point now) {
    return m_link.receive(m_index, packet, now);
}

void LoopbackLink::send(std::size_t to, std::span<const std::byte> packet,
                        Transport::Clock::time_point now) {
    const std::lock_guard lock(m_mutex);
    ++m_stats.sent;
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    if (unit(m_random) * 100.0f < m_conditions.lossPercent) {
        ++m_stats.lost;
        return;
    }
    const float delay = m_conditions.latencyMilliseconds +
                        unit(m_random) * std::max(m_conditions.jitterMilliseconds, 0.0f);
    m_inFlight[to].push_back({now + milliseconds(delay), m_nextOrder++,
                              std::vector<std::byte>(packet.begin(), packet.end())});
}

bool LoopbackLink::receive(std::size_t at, std::vector<std::byte>& packet,
                           Transport::Clock::time_point now) {
    const std::lock_guard lock(m_mutex);
    std::vector<InFlight>& queue = m_inFlight[at];
    const auto first =
        std::min_element(queue.begin(), queue.end(), [](const InFlight& a, const InFlight& b) {
            return a.arrival != b.arrival ? a.arrival < b.arrival : a.order < b.order;
        });
    if (first == queue.end() || first->arrival > now) {
        return false;
    }
    packet.assign(first->bytes.begin(), first->bytes.end());
    queue.erase(first);
    ++m_stats.delivered;
    return true;
}

} // namespace ct
