#ifndef TASH_STARSHIP_PROMPT_PROVIDER_H
#define TASH_STARSHIP_PROMPT_PROVIDER_H

#include "tash/plugin.h"
#include "tash/shell.h"
#include <string>
#include <vector>

// ── Build the starship argv from shell state ─────────────────
//
// Exposed for the unit test, which asserts the flag shape.

std::vector<std::string> build_starship_argv(const ShellState &state);

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
