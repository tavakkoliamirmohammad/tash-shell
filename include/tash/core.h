#ifndef TASH_CORE_H
#define TASH_CORE_H

// Umbrella header. Historically this pulled in the whole shell — parser,
// executor, signals, builtins, IR types, plus all their stdlib/replxx
// dependencies — for every TU. Now split into focused subheaders under
// `tash/core/*.h`; this umbrella re-includes them so existing callers
// need no churn. New code should prefer the narrower includes for
// better compile times.

#include "tash/shell.h"
#include "tash/core/builtins.h"
#include "tash/core/executor.h"
#include "tash/core/parser.h"
#include "tash/core/signals.h"

#endif // TASH_CORE_H
