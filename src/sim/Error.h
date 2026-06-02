#pragma once

// Defines lightweight command status values returned by the simulation API.
// Validation failures are reported without throwing so UI and CLI callers can
// present player-facing messages while the event log records the audit trail.

#include <string>
#include <utility>

namespace deep {

// Result of a command submitted to Simulation or SimulationService.
// ok=false means the command was rejected and should have no gameplay side effect
// beyond a CommandRejectedEvent being appended to the event log.
struct CommandResult {
    bool ok = false;
    std::string message;

    // Creates a successful result with an optional diagnostic message.
    [[nodiscard]] static CommandResult success(std::string message = {}) {
        return CommandResult{.ok = true, .message = std::move(message)};
    }

    // Creates a failed result. The message should be suitable for logs and UI.
    [[nodiscard]] static CommandResult failure(std::string message) {
        return CommandResult{.ok = false, .message = std::move(message)};
    }
};

} // namespace deep
