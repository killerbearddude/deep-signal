#pragma once

// Declares the first functional strategic map panel for the ImGui shell.
// The panel reads map DTOs through SimulationQueries and owns only UI-local
// camera/selection state.

#include "app/SimulationQueries.h"
#include "render/MapCamera.h"
#include "render/StrategicMapView.h"

#include <optional>

namespace deep::ui_imgui {

// Renders a simple home-system map with pan, zoom, labels, and marker picking.
// No simulation state is mutated and no raw GameState records are exposed here.
class StrategicMapPanel {
public:
    // Draws the strategic map using app-layer query DTOs.
    void render(const SimulationQueries& queries);

private:
    render::MapCamera camera_;
    render::StrategicMapView view_;
    std::optional<render::StrategicMapSelection> selection_;
};

} // namespace deep::ui_imgui
