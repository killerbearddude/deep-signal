#pragma once

// Declares the shared read-only inspector panel for the ImGui shell.
// The inspector consumes SelectionState plus query DTOs, keeping UI inspection
// independent from raw GameState storage.

#include "app/SelectionState.h"
#include "app/SimulationQueries.h"

namespace deep::ui_imgui {

// Shows details for the object selected by map and table panels.
// This panel is intentionally read-only; future edit commands should be added
// as explicit SimulationService command workflows.
class InspectorPanel {
public:
    // Draws details for the current selection using app-layer query summaries.
    void render(const SimulationQueries& queries, const SelectionState& selection) const;
};

} // namespace deep::ui_imgui
