#include "ui_imgui/StrategicMapPanel.h"

// Implements the basic strategic map panel using ImGui draw lists.
// Map picks are translated into shared SelectionState so the inspector and table
// panels all observe the same selected object.

#include <imgui.h>

#include <algorithm>
#include <optional>
#include <vector>

namespace deep::ui_imgui {
namespace {

constexpr float kMinimumCanvasWidth = 320.0F;
constexpr float kMinimumCanvasHeight = 240.0F;
constexpr double kWheelZoomIn = 1.20;
constexpr double kWheelZoomOut = 1.0 / kWheelZoomIn;
constexpr double kButtonZoomIn = 1.50;
constexpr double kButtonZoomOut = 1.0 / kButtonZoomIn;

[[nodiscard]] render::MapPoint toMapPoint(const ImVec2 value) noexcept {
    return render::MapPoint{.x = static_cast<double>(value.x), .y = static_cast<double>(value.y)};
}

[[nodiscard]] render::MapPoint viewportCenter(const ImVec2 canvasMin, const ImVec2 canvasSize) noexcept {
    return render::MapPoint{
        .x = static_cast<double>(canvasMin.x + (canvasSize.x * 0.5F)),
        .y = static_cast<double>(canvasMin.y + (canvasSize.y * 0.5F))
    };
}

[[nodiscard]] std::optional<render::StrategicMapSelection> mapSelectionFromSharedState(
    const SelectionState& selection,
    const std::vector<StrategicBodySummary>& bodies,
    const std::vector<StrategicFleetSummary>& fleets) {
    if (selection.type() == SelectedObjectType::Body) {
        const BodyId bodyId = selection.bodyId();
        const auto it = std::find_if(bodies.begin(), bodies.end(), [bodyId](const StrategicBodySummary& body) {
            return body.id == bodyId;
        });
        if (it != bodies.end()) {
            return render::StrategicMapSelection{
                .kind = render::StrategicMapSelection::Kind::Body,
                .id = it->id.value,
                .name = it->name
            };
        }
    }

    if (selection.type() == SelectedObjectType::Fleet) {
        const FleetId fleetId = selection.fleetId();
        const auto it = std::find_if(fleets.begin(), fleets.end(), [fleetId](const StrategicFleetSummary& fleet) {
            return fleet.id == fleetId;
        });
        if (it != fleets.end()) {
            return render::StrategicMapSelection{
                .kind = render::StrategicMapSelection::Kind::Fleet,
                .id = it->id.value,
                .name = it->name
            };
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<render::MapPoint> selectedMapAnchor(
    const SelectionState& selection,
    const std::vector<StrategicBodySummary>& bodies,
    const std::vector<StrategicFleetSummary>& fleets) {
    if (selection.type() == SelectedObjectType::Body) {
        const BodyId bodyId = selection.bodyId();
        const auto it = std::find_if(bodies.begin(), bodies.end(), [bodyId](const StrategicBodySummary& body) {
            return body.id == bodyId;
        });
        if (it != bodies.end()) {
            return render::MapPoint{.x = it->x, .y = it->y};
        }
    }

    if (selection.type() == SelectedObjectType::Fleet) {
        const FleetId fleetId = selection.fleetId();
        const auto it = std::find_if(fleets.begin(), fleets.end(), [fleetId](const StrategicFleetSummary& fleet) {
            return fleet.id == fleetId;
        });
        if (it != fleets.end()) {
            return render::MapPoint{.x = it->x, .y = it->y};
        }
    }

    return std::nullopt;
}

void applyMapSelection(const std::optional<render::StrategicMapSelection>& picked, SelectionState& selection) noexcept {
    if (!picked.has_value()) {
        selection.clear();
        return;
    }

    if (picked->kind == render::StrategicMapSelection::Kind::Body) {
        selection.selectBody(BodyId{picked->id});
    } else {
        selection.selectFleet(FleetId{picked->id});
    }
}

} // namespace

void StrategicMapPanel::render(const SimulationQueries& queries, SelectionState& selection, bool& visible) {
    if (!visible) {
        return;
    }

    const auto bodies = queries.strategicBodies();
    const auto fleets = queries.strategicFleets();

    if (!ImGui::Begin("Strategic Map", &visible)) {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted("Right-drag to pan. Mouse wheel to zoom. Left-click a marker to inspect or choose a move destination.");
    ImGui::TextUnformatted("Fleet routes show sustained-burn projected intercept arcs; low-energy transfers are not modeled yet.");
    const std::optional<render::MapPoint> selectedAnchor = selectedMapAnchor(selection, bodies, fleets);
    if (ImGui::Button("Zoom Out")) {
        // Toolbar zoom has no cursor anchor. When a map object is selected, keep
        // it centered so zooming in/out preserves operational context.
        if (selectedAnchor.has_value()) {
            camera_.centerOn(*selectedAnchor);
        }
        camera_.zoomAt(camera_.center(), kButtonZoomOut);
    }
    ImGui::SameLine();
    if (ImGui::Button("Zoom In")) {
        if (selectedAnchor.has_value()) {
            camera_.centerOn(*selectedAnchor);
        }
        camera_.zoomAt(camera_.center(), kButtonZoomIn);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset View")) {
        camera_ = render::MapCamera{};
    }
    ImGui::SameLine();
    ImGui::Text("Zoom %.3fx", camera_.zoom());

    ImVec2 available = ImGui::GetContentRegionAvail();
    available.x = std::max(available.x, kMinimumCanvasWidth);
    available.y = std::max(available.y - 72.0F, kMinimumCanvasHeight);

    const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("strategic-map-canvas", available,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    const bool rightDragging = ImGui::IsMouseDragging(ImGuiMouseButton_Right);

    if (hovered && rightDragging) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        camera_.panByScreenDelta(toMapPoint(delta));
    }

    if (hovered && ImGui::GetIO().MouseWheel != 0.0F) {
        if (selectedAnchor.has_value()) {
            // Selected markers become the zoom focus. This makes close inspection
            // predictable on the wide on-rails map where cursor-centered zooming
            // can otherwise push the selected fleet or body out of view.
            camera_.centerOn(*selectedAnchor);
            camera_.zoomAt(camera_.center(), ImGui::GetIO().MouseWheel > 0.0F ? kWheelZoomIn : kWheelZoomOut);
        } else {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const render::MapPoint anchor = camera_.screenToWorld(toMapPoint(mouse), viewportCenter(canvasMin, available));
            camera_.zoomAt(anchor, ImGui::GetIO().MouseWheel > 0.0F ? kWheelZoomIn : kWheelZoomOut);
        }
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        applyMapSelection(view_.pick(camera_, canvasMin, available, ImGui::GetIO().MousePos, bodies, fleets), selection);
    }

    const std::optional<render::StrategicMapSelection> mapSelection = mapSelectionFromSharedState(selection, bodies, fleets);
    view_.draw(*ImGui::GetWindowDrawList(), camera_, canvasMin, available, bodies, fleets, mapSelection);

    if (selection.type() != SelectedObjectType::None) {
        ImGui::Text("Selected: %s #%lld",
                    selection.type() == SelectedObjectType::Body ? "Body" :
                    selection.type() == SelectedObjectType::Colony ? "Colony" : "Fleet",
                    static_cast<long long>(selection.selectedId()));
    } else {
        ImGui::TextUnformatted("Selected: none");
    }

    ImGui::End();
}

} // namespace deep::ui_imgui
