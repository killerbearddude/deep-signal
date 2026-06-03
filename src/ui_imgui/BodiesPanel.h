#pragma once

// Declares the Bodies/System overview panel for the ImGui shell.
// The panel displays body-level counts from SimulationQueries and writes only
// typed selection IDs into SelectionState when a body row is clicked.

#include "app/SelectionState.h"
#include "app/SimulationQueries.h"

namespace deep::ui_imgui {

// Shows the high-level system/body roster so players can inspect the scenario
// without inferring all body context from map markers alone.
class BodiesPanel {
public:
    // Draws body rows and updates shared selection when the user clicks a body.
    void render(const SimulationQueries& queries, SelectionState& selection, bool& visible) const;
};

} // namespace deep::ui_imgui
