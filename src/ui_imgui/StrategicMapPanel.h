#pragma once

// Declares the first functional strategic map panel for the ImGui shell.
// The panel reads map DTOs through SimulationQueries and writes shared
// app-layer selection state for the inspector.

#include "app/SelectionState.h"
#include "app/SimulationQueries.h"
#include "render/MapCamera.h"
#include "render/StrategicMapView.h"

#include <optional>

namespace deep::ui_imgui {

// Renders a simple home-system map with pan, zoom, labels, and marker picking.
// No simulation state is mutated and no raw GameState records are exposed here.
class StrategicMapPanel {
public:
    // Draws the strategic map using app-layer query DTOs and updates close state.
    void render(const SimulationQueries& queries, SelectionState& selection, bool& visible);

private:
    // UI-only view state survives closing the panel and replacing the world.
    // Reset View restores the default camera; save files do not persist it.
    render::MapCamera camera_;
    render::StrategicMapView view_;
};

} // namespace deep::ui_imgui
