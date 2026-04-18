#ifndef TASH_STARSHIP_PROMPT_PROVIDER_H
#define TASH_STARSHIP_PROMPT_PROVIDER_H

#include "tash/plugin.h"
#include "tash/shell.h"
#include <string>

// ── Build the starship command string from shell state ────────
//
// Retained for the unit test, which asserts the flag shape. The
// render() entry point now builds an argv vector directly; see
// starship_prompt_provider.cpp.

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
