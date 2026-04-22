// FakeNotifier — header-only test double for INotifier.
// Every call is recorded. No queues, no canned responses — INotifier
// methods are void and one-way.

#ifndef TASH_CLUSTER_FAKE_NOTIFIER_H
#define TASH_CLUSTER_FAKE_NOTIFIER_H

#include "tash/cluster/notifier.h"

#include <string>
#include <vector>

namespace tash::cluster::testing {

class FakeNotifier : public INotifier {
public:
    struct DesktopCall { std::string title; std::string body; };

    std::vector<DesktopCall> desktop_calls;
    int                      bell_count = 0;

    void desktop(const std::string& title, const std::string& body) override {
        desktop_calls.push_back({title, body});
    }

    void bell() override {
        ++bell_count;
    }

    void reset() {
        desktop_calls.clear();
        bell_count = 0;
    }
};

}  // namespace tash::cluster::testing

#endif  // TASH_CLUSTER_FAKE_NOTIFIER_H
