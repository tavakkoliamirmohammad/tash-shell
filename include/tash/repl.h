#ifndef TASH_REPL_H
#define TASH_REPL_H

#include "tash/shell.h"

namespace tash {

// Enter the interactive read-eval-print loop: print the banner, set up
// replxx (keybindings, highlighter, hints), read lines, dispatch to the
// executor. Returns when the user exits or Ctrl-Ds twice.
int run_interactive(ShellState &state);

} // namespace tash

#endif // TASH_REPL_H
