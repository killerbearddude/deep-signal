#include "render/MapCamera.h"

// Implements simple pan/zoom math for the ImGui strategic map.
// There is intentionally no orbital model here; world coordinates come directly
// from the deterministic prototype scenario.

#include <algorithm>

namespace deep::render {
namespace {

constexpr double kMinimumZoom = 0.25;
constexpr double kMaximumZoom = 20.0;

} // namespace

MapCamera::MapCamera() noexcept
    // Center between Terra at x=0 and Mars at x=240 in the home scenario.
    : center_{.x = 120.0, .y = 0.0}, zoom_{2.0} {}

MapPoint MapCamera::worldToScreen(const MapPoint world, const MapPoint viewportCenter) const noexcept {
    return MapPoint{
        .x = viewportCenter.x + ((world.x - center_.x) * zoom_),
        .y = viewportCenter.y + ((world.y - center_.y) * zoom_)
    };
}

MapPoint MapCamera::screenToWorld(const MapPoint screen, const MapPoint viewportCenter) const noexcept {
    return MapPoint{
        .x = center_.x + ((screen.x - viewportCenter.x) / zoom_),
        .y = center_.y + ((screen.y - viewportCenter.y) / zoom_)
    };
}

void MapCamera::panByScreenDelta(const MapPoint screenDelta) noexcept {
    // Dragging the canvas right should move the camera left in world space,
    // matching common map interaction behavior.
    center_.x -= screenDelta.x / zoom_;
    center_.y -= screenDelta.y / zoom_;
}

void MapCamera::zoomAt(const MapPoint worldAnchor, const double zoomFactor) noexcept {
    const double previousZoom = zoom_;
    zoom_ = std::clamp(zoom_ * zoomFactor, kMinimumZoom, kMaximumZoom);

    if (previousZoom == zoom_) {
        return;
    }

    // Shift the center toward the anchor proportionally to the scale change so
    // mouse-wheel zoom feels anchored instead of jumping around the map.
    const double anchorWeight = 1.0 - (previousZoom / zoom_);
    center_.x += (worldAnchor.x - center_.x) * anchorWeight;
    center_.y += (worldAnchor.y - center_.y) * anchorWeight;
}

double MapCamera::zoom() const noexcept {
    return zoom_;
}

MapPoint MapCamera::center() const noexcept {
    return center_;
}

} // namespace deep::render
