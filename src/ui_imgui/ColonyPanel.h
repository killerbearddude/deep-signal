#pragma once

// Declares the ImGui colony summary panel.
// The panel consumes app-layer DTOs from SimulationQueries instead of inspecting
// GameState vectors directly.

#include "app/SelectionState.h"
#include "app/SimulationQueries.h"

namespace deep::ui_imgui {

// Renders colony overview rows using stable query summaries prepared for UI use.
class ColonyPanel {
public:
    // Draws one table row per colony summary and updates visibility when closed.
    void render(const SimulationQueries& queries, SelectionState& selection, bool& visible) const;
};

} // namespace deep::ui_imgui
