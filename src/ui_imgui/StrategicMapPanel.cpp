#include "ui_imgui/StrategicMapPanel.h"

// Implements the basic strategic map panel using ImGui draw lists.
// Interaction is intentionally limited to view navigation and selection so this
// patch does not introduce gameplay commands or a complex renderer.

#include <imgui.h>

#include <algorithm>

namespace deep::ui_imgui {
namespace {

constexpr float kMinimumCanvasWidth = 320.0F;
constexpr float kMinimumCanvasHeight = 240.0F;
constexpr double kWheelZoomIn = 1.12;
constexpr double kWheelZoomOut = 1.0 / kWheelZoomIn;

[[nodiscard]] render::MapPoint toMapPoint(const ImVec2 value) noexcept {
    return render::MapPoint{.x = static_cast<double>(value.x), .y = static_cast<double>(value.y)};
}

[[nodiscard]] render::MapPoint viewportCenter(const ImVec2 canvasMin, const ImVec2 canvasSize) noexcept {
    return render::MapPoint{
        .x = static_cast<double>(canvasMin.x + (canvasSize.x * 0.5F)),
        .y = static_cast<double>(canvasMin.y + (canvasSize.y * 0.5F))
    };
}

} // namespace

void StrategicMapPanel::render(const SimulationQueries& queries) {
    const auto bodies = queries.strategicBodies();
    const auto fleets = queries.strategicFleets();

    ImGui::Begin("Strategic Map");
    ImGui::TextUnformatted("Right-drag to pan. Mouse wheel to zoom. Left-click a marker to select.");

    ImVec2 available = ImGui::GetContentRegionAvail();
    available.x = std::max(available.x, kMinimumCanvasWidth);
    available.y = std::max(available.y - 48.0F, kMinimumCanvasHeight);

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
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const render::MapPoint anchor = camera_.screenToWorld(toMapPoint(mouse), viewportCenter(canvasMin, available));
        camera_.zoomAt(anchor, ImGui::GetIO().MouseWheel > 0.0F ? kWheelZoomIn : kWheelZoomOut);
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        selection_ = view_.pick(camera_, canvasMin, available, ImGui::GetIO().MousePos, bodies, fleets);
    }

    view_.draw(*ImGui::GetWindowDrawList(), camera_, canvasMin, available, bodies, fleets, selection_);

    if (selection_.has_value()) {
        ImGui::Text("Selected: %s #%lld - %s",
                    selection_->kind == render::StrategicMapSelection::Kind::Body ? "Body" : "Fleet",
                    static_cast<long long>(selection_->id),
                    selection_->name.c_str());
    } else {
        ImGui::TextUnformatted("Selected: none");
    }

    ImGui::End();
}

} // namespace deep::ui_imgui
