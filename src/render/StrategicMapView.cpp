#include "render/StrategicMapView.h"

// Implements the first strategic map renderer using only ImGui draw primitives.
// This is deliberately simple: bodies and fleets are markers over abstract
// scenario coordinates, not orbital mechanics or tactical rendering.

#include <limits>
#include <string>

namespace deep::render {
namespace {

constexpr float kBodyRadius = 8.0F;
constexpr float kFleetRadius = 5.0F;
constexpr float kPickRadius = 14.0F;
constexpr float kGridSpacing = 80.0F;
[[nodiscard]] MapPoint viewportCenter(const ImVec2 canvasMin, const ImVec2 canvasSize) noexcept {
    return MapPoint{
        .x = static_cast<double>(canvasMin.x + (canvasSize.x * 0.5F)),
        .y = static_cast<double>(canvasMin.y + (canvasSize.y * 0.5F))
    };
}

[[nodiscard]] ImVec2 toImVec2(const MapPoint point) noexcept {
    return ImVec2{static_cast<float>(point.x), static_cast<float>(point.y)};
}

[[nodiscard]] MapPoint bodyPoint(const StrategicBodySummary& body) noexcept {
    return MapPoint{.x = body.x, .y = body.y};
}

[[nodiscard]] MapPoint fleetPoint(const StrategicFleetSummary& fleet) noexcept {
    return MapPoint{.x = fleet.x, .y = fleet.y};
}

[[nodiscard]] MapPoint fleetDestinationPoint(const StrategicFleetSummary& fleet) noexcept {
    return MapPoint{.x = fleet.destinationX, .y = fleet.destinationY};
}

[[nodiscard]] double distanceSquared(const ImVec2 a, const ImVec2 b) noexcept {
    const double dx = static_cast<double>(a.x - b.x);
    const double dy = static_cast<double>(a.y - b.y);
    return (dx * dx) + (dy * dy);
}

[[nodiscard]] bool isSelected(const std::optional<StrategicMapSelection>& selection,
                              const StrategicMapSelection::Kind kind,
                              const std::int64_t id) noexcept {
    return selection.has_value() && selection->kind == kind && selection->id == id;
}

void drawGrid(ImDrawList& drawList, const ImVec2 canvasMin, const ImVec2 canvasMax) {
    // Screen-space grid is only an orientation aid; it is not a simulation scale.
    for (float x = canvasMin.x; x < canvasMax.x; x += kGridSpacing) {
        drawList.AddLine(ImVec2{x, canvasMin.y}, ImVec2{x, canvasMax.y}, IM_COL32(55, 55, 60, 255));
    }
    for (float y = canvasMin.y; y < canvasMax.y; y += kGridSpacing) {
        drawList.AddLine(ImVec2{canvasMin.x, y}, ImVec2{canvasMax.x, y}, IM_COL32(55, 55, 60, 255));
    }
}

void drawBody(ImDrawList& drawList,
              const MapCamera& camera,
              const MapPoint viewCenter,
              const StrategicBodySummary& body,
              const bool selected) {
    const ImVec2 position = toImVec2(camera.worldToScreen(bodyPoint(body), viewCenter));
    const float radius = selected ? kBodyRadius + 3.0F : kBodyRadius;

    drawList.AddCircleFilled(position, radius, IM_COL32(120, 170, 255, 255), 24);
    drawList.AddCircle(position, radius + 1.0F, selected ? IM_COL32(255, 230, 120, 255) : IM_COL32(220, 220, 230, 255), 24, 2.0F);
    drawList.AddText(ImVec2{position.x + 12.0F, position.y - 8.0F}, IM_COL32(235, 235, 235, 255), body.name.c_str());
}

void drawFleet(ImDrawList& drawList,
               const MapCamera& camera,
               const MapPoint viewCenter,
               const StrategicFleetSummary& fleet,
               const bool selected) {
    const ImVec2 position = toImVec2(camera.worldToScreen(fleetPoint(fleet), viewCenter));
    const float radius = selected ? kFleetRadius + 3.0F : kFleetRadius;

    if (fleet.moving && fleet.destinationBodyId.has_value()) {
        const ImVec2 destination = toImVec2(camera.worldToScreen(fleetDestinationPoint(fleet), viewCenter));
        drawList.AddLine(position, destination, IM_COL32(120, 220, 150, 180), 1.5F);
    }

    drawList.AddRectFilled(ImVec2{position.x - radius, position.y - radius},
                           ImVec2{position.x + radius, position.y + radius},
                           IM_COL32(120, 240, 160, 255));
    drawList.AddRect(ImVec2{position.x - radius - 1.0F, position.y - radius - 1.0F},
                     ImVec2{position.x + radius + 1.0F, position.y + radius + 1.0F},
                     selected ? IM_COL32(255, 230, 120, 255) : IM_COL32(220, 250, 220, 255));
    drawList.AddText(ImVec2{position.x + 10.0F, position.y + 4.0F}, IM_COL32(220, 250, 220, 255), fleet.name.c_str());
}

} // namespace

void StrategicMapView::draw(ImDrawList& drawList,
                            const MapCamera& camera,
                            const ImVec2 canvasMin,
                            const ImVec2 canvasSize,
                            const std::vector<StrategicBodySummary>& bodies,
                            const std::vector<StrategicFleetSummary>& fleets,
                            const std::optional<StrategicMapSelection>& selection) const {
    const ImVec2 canvasMax{canvasMin.x + canvasSize.x, canvasMin.y + canvasSize.y};
    const MapPoint viewCenter = viewportCenter(canvasMin, canvasSize);

    drawList.AddRectFilled(canvasMin, canvasMax, IM_COL32(20, 22, 28, 255));
    drawGrid(drawList, canvasMin, canvasMax);

    // Clip map contents to the child canvas while still allowing labels to draw
    // near markers without interfering with surrounding docked panels.
    drawList.PushClipRect(canvasMin, canvasMax, true);
    for (const StrategicBodySummary& body : bodies) {
        drawBody(drawList, camera, viewCenter, body,
                 isSelected(selection, StrategicMapSelection::Kind::Body, body.id.value));
    }

    for (const StrategicFleetSummary& fleet : fleets) {
        drawFleet(drawList, camera, viewCenter, fleet,
                  isSelected(selection, StrategicMapSelection::Kind::Fleet, fleet.id.value));
    }
    drawList.PopClipRect();

    const MapPoint center = camera.center();
    const std::string overlay = "Zoom " + std::to_string(camera.zoom()).substr(0, 4)
        + " | Center " + std::to_string(center.x).substr(0, 6) + ", " + std::to_string(center.y).substr(0, 6);
    drawList.AddText(ImVec2{canvasMin.x + 8.0F, canvasMin.y + 8.0F}, IM_COL32(180, 185, 195, 255), overlay.c_str());
}

std::optional<StrategicMapSelection> StrategicMapView::pick(const MapCamera& camera,
                                                            const ImVec2 canvasMin,
                                                            const ImVec2 canvasSize,
                                                            const ImVec2 screenPoint,
                                                            const std::vector<StrategicBodySummary>& bodies,
                                                            const std::vector<StrategicFleetSummary>& fleets) const {
    const MapPoint viewCenter = viewportCenter(canvasMin, canvasSize);
    const double maxDistanceSquared = static_cast<double>(kPickRadius * kPickRadius);
    double bestDistanceSquared = std::numeric_limits<double>::max();
    std::optional<StrategicMapSelection> bestSelection;

    // Prefer the closest marker regardless of type so overlapping future map
    // objects produce deterministic picks without exposing UI-specific state.
    for (const StrategicFleetSummary& fleet : fleets) {
        const ImVec2 marker = toImVec2(camera.worldToScreen(fleetPoint(fleet), viewCenter));
        const double distance = distanceSquared(marker, screenPoint);
        if (distance <= maxDistanceSquared && distance < bestDistanceSquared) {
            bestDistanceSquared = distance;
            bestSelection = StrategicMapSelection{
                .kind = StrategicMapSelection::Kind::Fleet,
                .id = fleet.id.value,
                .name = fleet.name
            };
        }
    }

    for (const StrategicBodySummary& body : bodies) {
        const ImVec2 marker = toImVec2(camera.worldToScreen(bodyPoint(body), viewCenter));
        const double distance = distanceSquared(marker, screenPoint);
        if (distance <= maxDistanceSquared && distance < bestDistanceSquared) {
            bestDistanceSquared = distance;
            bestSelection = StrategicMapSelection{
                .kind = StrategicMapSelection::Kind::Body,
                .id = body.id.value,
                .name = body.name
            };
        }
    }

    return bestSelection;
}

} // namespace deep::render
