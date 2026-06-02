#pragma once

// Declares the lightweight ImGui draw-list renderer for the strategic map.
// The renderer consumes app-layer map DTOs only; it never reads GameState or
// owns simulation data.

#include "app/SimulationQueries.h"
#include "render/MapCamera.h"
#include "sim/IdTypes.h"

#include <imgui.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace deep::render {

// Identifies the selected map object without exposing raw pointers or records.
struct StrategicMapSelection {
    enum class Kind {
        Body,
        Fleet
    };

    Kind kind = Kind::Body;
    std::int64_t id = 0;
    std::string name;
};

// Draws bodies, fleet markers, labels, and simple movement lines into an ImGui
// canvas. Interaction state stays in the owning UI panel.
class StrategicMapView {
public:
    // Renders the current map DTOs into the supplied ImGui canvas rectangle.
    void draw(ImDrawList& drawList,
              const MapCamera& camera,
              ImVec2 canvasMin,
              ImVec2 canvasSize,
              const std::vector<StrategicBodySummary>& bodies,
              const std::vector<StrategicFleetSummary>& fleets,
              const std::optional<StrategicMapSelection>& selection) const;

    // Finds the nearest body or fleet marker to a clicked screen-space point.
    // Returns no selection when the click is outside the small marker hit radius.
    [[nodiscard]] std::optional<StrategicMapSelection> pick(const MapCamera& camera,
                                                            ImVec2 canvasMin,
                                                            ImVec2 canvasSize,
                                                            ImVec2 screenPoint,
                                                            const std::vector<StrategicBodySummary>& bodies,
                                                            const std::vector<StrategicFleetSummary>& fleets) const;
};

} // namespace deep::render
