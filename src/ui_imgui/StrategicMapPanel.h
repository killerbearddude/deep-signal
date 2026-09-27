#pragma once

// Declares the first functional strategic map panel for the ImGui shell.
// The panel reads map DTOs through SimulationQueries and writes shared
// generation-stamped selection intentions for the inspector.

#include "app/InformationInteractionAdapter.h"
#include "app/SimulationQueries.h"
#include "render/MapCamera.h"
#include "render/StrategicMapView.h"

#include <optional>

namespace deep::ui_imgui {

// Renders a simple home-system map with pan, zoom, labels, and marker picking.
// No simulation state is mutated and no raw GameState records are exposed here.
class StrategicMapPanel {
public:
    // Request one presentation-only reveal of a body or fleet in this world.
    // The current map DTO supplies the position when the panel next renders.
    void requestReveal(ObjectReference target);

    // Draws the strategic map using app-layer query DTOs and updates close state.
    void render(const SimulationQueries& queries, InformationInteractionAdapter& interactions, bool& visible);

private:
    friend struct InformationRevealTestAccess;
    // UI-only view state survives closing the panel and replacing the world.
    // Reset View restores the default camera; save files do not persist it.
    render::MapCamera camera_;
    render::StrategicMapView view_;
    std::optional<ObjectReference> pendingReveal_;
};

} // namespace deep::ui_imgui
