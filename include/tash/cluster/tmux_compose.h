// Pure tmux command + parser helpers.
//
// Shell-quoting: every tmux argument we want to send through ssh gets
// wrapped in single quotes, with internal single quotes replaced by the
// canonical '\'' dance. That keeps argv-over-ssh safe against spaces,
// dollar signs, quotes, semicolons, backticks, newlines — anything the
// remote shell would otherwise interpret.
//
// Command composition:
//   tmux_cmd_new_session  / _new_window / _list_sessions / _list_windows
//   / _kill_window / _is_alive produce tmux subcommand strings (already
//   fully shell-quoted).
//   compose_remote_cmd(target, tmux_cmd) wraps in `ssh <node> <tmux_cmd>`
//   when target.node is set; passes straight through when it isn't.
//
// Parsers:
//   parse_list_sessions, parse_list_windows.
//
// This header is pure — no I/O, no ssh, no process spawning. The
// stateful TmuxOps impl composes these with an injected ISshClient.

#ifndef TASH_CLUSTER_TMUX_COMPOSE_H
#define TASH_CLUSTER_TMUX_COMPOSE_H

#include "tash/cluster/types.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tash::cluster::tmux_compose {

// Single-quote-wrap with '\'' for embedded quotes.
std::string shell_quote(std::string_view s);

// tmux command builders. Returned strings are ready to pass as one
// remote command to ssh — no further quoting needed at the call site.
std::string tmux_new_session  (const std::string& session, const std::string& cwd);
std::string tmux_new_window   (const std::string& session, const std::string& window,
                                 const std::string& cwd,    const std::string& cmd);
std::string tmux_list_sessions ();
std::string tmux_list_windows  (const std::string& session);
std::string tmux_kill_window   (const std::string& session, const std::string& window);
std::string tmux_is_alive      (const std::string& session, const std::string& window);

// If target.node is empty, returns inner verbatim. Otherwise wraps as
// "ssh <shell-quoted-node> <shell-quoted-inner>" so the outer ssh
// (to the login node) can shell out to the compute node.
std::string compose_remote_cmd(const RemoteTarget& target, const std::string& inner);

// Argv for the attach flow. Uses `ssh -t` for pty allocation (tmux
// needs a real terminal). Production impl `execvp`s this — tests
// verify the argv shape but never actually exec.
std::vector<std::string> build_attach_argv(const RemoteTarget& target,
                                              const std::string& session,
                                              const std::string& window);

// Parsers. Expected formats (both produced by the builder's -F spec):
//   list-sessions: `<name>|<window-count>|<0|1 attached>` per line
//   list-windows:  `<window-name>|<pane-pid>` per line
std::vector<SessionInfo>                              parse_list_sessions(std::string_view);
std::vector<std::pair<std::string, long>>             parse_list_windows (std::string_view);

}  // namespace tash::cluster::tmux_compose

#endif  // TASH_CLUSTER_TMUX_COMPOSE_H
