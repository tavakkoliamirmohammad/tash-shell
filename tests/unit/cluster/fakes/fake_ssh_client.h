// FakeSshClient — header-only test double for ISshClient.
//
// Every call is recorded in a public vector; return values for run() are
// pulled from a FIFO queue. Empty queue = default-constructed SshResult
// (exit_code=0, empty out/err). Tests that care about specific outputs
// queue them; tests that only care about which commands were issued
// ignore the queue and inspect the `run_calls` vector.

#ifndef TASH_CLUSTER_FAKE_SSH_CLIENT_H
#define TASH_CLUSTER_FAKE_SSH_CLIENT_H

#include "tash/cluster/ssh_client.h"

#include <chrono>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace tash::cluster::testing {

class FakeSshClient : public ISshClient {
public:
    struct RunCall {
        std::string cluster;
        std::vector<std::string> argv;
        std::chrono::milliseconds timeout;
    };

    // Recorded invocations (public; tests read directly).
    std::vector<RunCall>     run_calls;
    std::vector<std::string> connect_calls;
    std::vector<std::string> disconnect_calls;

    // Per-cluster master state. connect() sets true, disconnect() sets false.
    std::unordered_map<std::string, bool> master_state;

    // FIFO queue of SshResults for run(). Empty queue -> default SshResult.
    std::deque<SshResult> run_queue;

    // ── Scripting helpers ──────────────────────────────────
    void queue_run(SshResult r)                           { run_queue.push_back(std::move(r)); }
    void set_master_alive(const std::string& c, bool v)   { master_state[c] = v; }

    // ── ISshClient ────────────────────────────────────────
    SshResult run(const std::string& cluster,
                   const std::vector<std::string>& argv,
                   std::chrono::milliseconds timeout) override {
        run_calls.push_back(RunCall{cluster, argv, timeout});
        if (run_queue.empty()) return SshResult{};
        SshResult r = run_queue.front();
        run_queue.pop_front();
        return r;
    }

    bool master_alive(const std::string& cluster) override {
        auto it = master_state.find(cluster);
        return it != master_state.end() && it->second;
    }

    void connect(const std::string& cluster) override {
        connect_calls.push_back(cluster);
        master_state[cluster] = true;
    }

    void disconnect(const std::string& cluster) override {
        disconnect_calls.push_back(cluster);
        master_state[cluster] = false;
    }

    // ── Test-only ─────────────────────────────────────────
    void reset() {
        run_calls.clear();
        connect_calls.clear();
        disconnect_calls.clear();
        master_state.clear();
        run_queue.clear();
    }
};

}  // namespace tash::cluster::testing

#endif  // TASH_CLUSTER_FAKE_SSH_CLIENT_H
