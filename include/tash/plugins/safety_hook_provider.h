#ifndef TASH_PLUGINS_SAFETY_HOOK_PROVIDER_H
#define TASH_PLUGINS_SAFETY_HOOK_PROVIDER_H

#include "tash/plugin.h"
#include <string>
#include <vector>

// ── Risk levels for command classification ───────────────────

enum RiskLevel { BLOCKED, HIGH, MEDIUM, SAFE };

// ── Danger pattern descriptor ────────────────────────────────

struct DangerPattern {
    std::string regex_pattern;  // used as label / documentation only
    RiskLevel level;
    std::string warning_message;
};

// ── Pure classification function (testable without I/O) ──────

RiskLevel classify_command(const std::string &cmd);

// ── Safety Hook Provider ─────────────────────────────────────

class SafetyHookProvider : public IHookProvider {
public:
    std::string name() const override;
    void on_before_command(const std::string &command,
                           ShellState &state) override;
    void on_after_command(const std::string &command,
                          int exit_code,
                          const std::string &stderr_output,
                          ShellState &state) override;
};

#endif // TASH_PLUGINS_SAFETY_HOOK_PROVIDER_H
