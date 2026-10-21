#pragma once

#include "croftech/server/Server.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace ct {

// Runs a server on its own thread at exactly kTickRate: each tick it reads the packets that have
// arrived, steps the simulation and sends snapshots.
class ServerThread {
public:
    using Clock = std::chrono::steady_clock;

    // The server must outlive the thread.
    explicit ServerThread(Server& server) : m_server(server) {}
    ~ServerThread();
    ServerThread(const ServerThread&) = delete;
    ServerThread& operator=(const ServerThread&) = delete;

    void start();
    void stop();

    // Calls fn(server) between ticks, for changing settings or reading statistics.
    template <typename Fn>
    void withServer(Fn&& fn) {
        const std::lock_guard lock(m_mutex);
        fn(m_server);
    }

    float lastTickMilliseconds() const { return m_lastTickMilliseconds.load(); }

private:
    void run(std::stop_token stop);

    Server& m_server;
    std::mutex m_mutex;
    std::atomic<float> m_lastTickMilliseconds{0.0f};
    std::jthread m_thread;
};

} // namespace ct
