#ifndef TASH_STARSHIP_PROMPT_PROVIDER_H
#define TASH_STARSHIP_PROMPT_PROVIDER_H

#include "tash/plugin.h"
#include "tash/shell.h"
#include <string>

// ── Helper: run a command and capture stdout ─────────────────

std::string popen_read(const std::string &command);

// ── Build the starship command string from shell state ────────

std::string build_starship_command(const ShellState &state);

// ── Starship prompt provider ─────────────────────────────────

class StarshipPromptProvider : public IPromptProvider {
public:
    std::string name() const override { return "starship"; }
    int priority() const override { return 20; }
    std::string render(const ShellState &state) override;

    static bool is_available();

private:
    static bool cached_;
    static bool available_;
};

#endif // TASH_STARSHIP_PROMPT_PROVIDER_H
