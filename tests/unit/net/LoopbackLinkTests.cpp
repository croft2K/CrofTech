#include "croftech/net/LoopbackLink.h"

#include <doctest/doctest.h>

#include <chrono>
#include <cstddef>
#include <vector>

namespace {

using Clock = ct::Transport::Clock;
using std::chrono::milliseconds;

const Clock::time_point kStart{};

std::vector<std::byte> packet(int value) { return {static_cast<std::byte>(value)}; }

} // namespace

TEST_CASE("a perfect link delivers at once, in order, each way") {
    ct::LoopbackLink link;
    link.clientEnd().send(packet(1), kStart);
    link.clientEnd().send(packet(2), kStart);
    link.serverEnd().send(packet(3), kStart);
    std::vector<std::byte> received;
    REQUIRE(link.serverEnd().receive(received, kStart));
    CHECK(received == packet(1));
    REQUIRE(link.serverEnd().receive(received, kStart));
    CHECK(received == packet(2));
    CHECK_FALSE(link.serverEnd().receive(received, kStart));
    REQUIRE(link.clientEnd().receive(received, kStart));
    CHECK(received == packet(3));
}

TEST_CASE("latency holds packets back") {
    ct::LoopbackLink link({50.0f, 0.0f, 0.0f});
    link.clientEnd().send(packet(1), kStart);
    std::vector<std::byte> received;
    CHECK_FALSE(link.serverEnd().receive(received, kStart + milliseconds(49)));
    CHECK(link.serverEnd().receive(received, kStart + milliseconds(50)));
}

TEST_CASE("jitter can reorder packets but loses none") {
    ct::LoopbackLink link({20.0f, 30.0f, 0.0f}, 7);
    for (int i = 0; i < 200; ++i) {
        link.clientEnd().send(packet(i), kStart + milliseconds(i));
    }
    std::vector<std::byte> received;
    int count = 0;
    bool reordered = false;
    int last = -1;
    while (link.serverEnd().receive(received, kStart + milliseconds(1000))) {
        const int value = static_cast<int>(received[0]);
        reordered = reordered || value < last;
        last = value;
        ++count;
    }
    CHECK(count == 200);
    CHECK(reordered);
}

TEST_CASE("loss drops about the share asked for") {
    ct::LoopbackLink link({0.0f, 0.0f, 10.0f}, 3);
    std::vector<std::byte> received;
    int delivered = 0;
    for (int i = 0; i < 10000; ++i) {
        link.clientEnd().send(packet(0), kStart);
        delivered += link.serverEnd().receive(received, kStart) ? 1 : 0;
    }
    CHECK(delivered > 8800);
    CHECK(delivered < 9200);
    const ct::LinkStats stats = link.stats();
    CHECK(stats.sent == 10000);
    CHECK(stats.lost + stats.delivered == 10000);
}
