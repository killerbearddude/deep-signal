#pragma once

// Declares object inspection and direct fleet-command controls for the ImGui shell.
// The inspector consumes SelectionState plus query DTOs, keeping UI inspection
// independent from raw GameState storage. Command buttons route mutations through
// SimulationService so the UI never edits simulation records directly.

#include "app/SelectionState.h"
#include "app/SimulationQueries.h"
#include "app/SimulationService.h"
#include "sim/IdTypes.h"

#include <optional>
#include <string>

namespace deep::ui_imgui {

// Shows details for the object selected by map and table panels.
// The panel also owns lightweight UI-only command status for prototype fleet
// movement; all actual mutation still goes through SimulationService commands.
class InspectorPanel {
public:
    // Draws details for the current selection, command controls for valid
    // selection combinations, and updates visibility when closed.
    void render(const SimulationQueries& queries,
                SimulationService& service,
                const SelectionState& selection,
                bool& visible);

    // Called synchronously only after a successful world replacement, even when
    // hidden. Preserve normal source-fleet memory across ordinary selection.
    void resetWorldState() {
        fleetMoveSource_.reset();
        lastMoveStatus_ = "Ready";
        lastMoveSucceeded_ = true;
        lastCancelStatus_ = "No fleet order cancelled yet";
        lastCancelSucceeded_ = true;
    }

private:
    // Headless integration fixtures verify the real hook without rendering UI.
    friend struct InformationLifecycleTestAccess;
    // Remembers the fleet selected for a future body-click movement command.
    // This is UI workflow state only; the authoritative fleet state remains in
    // SimulationService and is re-queried before a command is submitted.
    std::optional<FleetId> fleetMoveSource_;
    std::string lastMoveStatus_ = "Ready";
    bool lastMoveSucceeded_ = true;
    std::string lastCancelStatus_ = "No fleet order cancelled yet";
    bool lastCancelSucceeded_ = true;
};

} // namespace deep::ui_imgui
