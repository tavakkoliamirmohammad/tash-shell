// Notification seam. Real platform impls land in M3:
//   - src/cluster/notifier_mac.cpp    (osascript)
//   - src/cluster/notifier_linux.cpp  (notify-send)
// Test fake in tests/unit/cluster/fakes/fake_notifier.h records calls.

#ifndef TASH_CLUSTER_NOTIFIER_H
#define TASH_CLUSTER_NOTIFIER_H

#include <string>

namespace tash::cluster {

class INotifier {
public:
    virtual ~INotifier() = default;

    virtual void desktop(const std::string& title, const std::string& body) = 0;
    virtual void bell() = 0;
};

}  // namespace tash::cluster

#endif  // TASH_CLUSTER_NOTIFIER_H
