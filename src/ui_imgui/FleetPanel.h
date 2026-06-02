#pragma once

// Declares the ImGui fleet summary panel.
// Fleet rows are read from SimulationQueries so the UI cannot mutate or retain
// references into GameState vectors.

#include "app/SelectionState.h"
#include "app/SimulationQueries.h"

namespace deep::ui_imgui {

// Renders fleet location, ship count, and active-order state.
class FleetPanel {
public:
    // Draws one table row per fleet summary and updates visibility when closed.
    void render(const SimulationQueries& queries, SelectionState& selection, bool& visible) const;
};

} // namespace deep::ui_imgui
