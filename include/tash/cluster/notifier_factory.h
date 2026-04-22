// Platform-specific INotifier factory.
//
// macOS  -> spawns `osascript -e 'display notification …'`
// Linux  -> spawns `notify-send <title> <body>`
// Other  -> NoOpNotifier (silent fallback; ensures no platform builds
//            end up without a notifier)
//
// bell() on every platform emits "\a" to stderr — portable enough.

#ifndef TASH_CLUSTER_NOTIFIER_FACTORY_H
#define TASH_CLUSTER_NOTIFIER_FACTORY_H

#include "tash/cluster/notifier.h"

#include <memory>

namespace tash::cluster {

std::unique_ptr<INotifier> make_notifier();

}  // namespace tash::cluster

#endif  // TASH_CLUSTER_NOTIFIER_FACTORY_H
