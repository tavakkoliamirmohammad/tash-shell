#ifndef TASH_STARTUP_H
#define TASH_STARTUP_H

#include "tash/shell.h"

namespace tash {

// Register every bundled provider (hooks, completion, prompt, history)
// respecting the TASH_DISABLE_* env-var opt-out. Safe to call once at
// startup; re-entering would duplicate registrations.
void register_default_plugins();

// Parse ~/.tashrc and execute each non-empty line in `state`. Silent
// no-op when the file doesn't exist.
void load_tashrc(ShellState &state);

// One-shot benchmark mode: time every startup stage, print the report,
// and return exit code 0. Does not enter the REPL.
int run_benchmark_mode();

} // namespace tash

#endif // TASH_STARTUP_H
