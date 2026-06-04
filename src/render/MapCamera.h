#pragma once

// Defines a tiny 2D camera for the prototype strategic map.
// The camera is renderer-agnostic: it stores world center and zoom only, while
// ImGui-specific draw code lives in StrategicMapView/StrategicMapPanel.

namespace deep::render {

// Lightweight 2D point used by the map camera without depending on ImGui types.
struct MapPoint {
    double x = 0.0;
    double y = 0.0;
};

// Converts between abstract simulation map coordinates and panel pixels.
// Positive zoom means pixels per one simulation coordinate unit.
class MapCamera {
public:
    // Creates a camera centered on the home system with a conservative zoom that
    // keeps the expanded on-rails scenario visible by default.
    MapCamera() noexcept;

    // Converts a world-space point into screen-space pixels relative to the
    // current map canvas.
    [[nodiscard]] MapPoint worldToScreen(MapPoint world, MapPoint viewportCenter) const noexcept;

    // Converts screen-space pixels relative to the current map canvas back into
    // simulation world coordinates for picking.
    [[nodiscard]] MapPoint screenToWorld(MapPoint screen, MapPoint viewportCenter) const noexcept;

    // Pans the camera by a dragged mouse delta in screen pixels.
    void panByScreenDelta(MapPoint screenDelta) noexcept;

    // Zooms around a chosen world-space anchor. This keeps the point underneath
    // the cursor visually stable while the wheel changes scale.
    void zoomAt(MapPoint worldAnchor, double zoomFactor) noexcept;

    // Recenters the camera on a world-space object. Toolbar and wheel zoom use
    // this for selected markers so repeated zooming keeps the selected object
    // in the middle of the map instead of drifting off-screen.
    void centerOn(MapPoint worldCenter) noexcept;

    [[nodiscard]] double zoom() const noexcept;
    [[nodiscard]] MapPoint center() const noexcept;

private:
    MapPoint center_{};
    double zoom_ = 0.20;
};

} // namespace deep::render
