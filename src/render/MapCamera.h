#pragma once

// Defines a tiny 2D camera for the prototype strategic map.
// The camera is renderer-agnostic: it stores world center and zoom only, while
// ImGui-specific draw code lives in StrategicMapView/StrategicMapPanel.

namespace deep::render {

// Lightweight 2D point used without ImGui types. The calling API determines
// whether its components are map units or absolute screen pixels.
struct MapPoint {
    double x = 0.0;
    double y = 0.0;
};

// Converts between abstract simulation map coordinates and panel pixels.
// Positive zoom means pixels per one simulation coordinate unit.
// Callers supply finite coordinates and finite positive zoom factors. The camera
// clamps zoom but does not validate non-finite input; it owns no world geometry.
class MapCamera {
public:
    // Creates a camera centered on the home system with a conservative zoom that
    // keeps the expanded on-rails scenario visible by default.
    MapCamera() noexcept;

    // Converts map units into absolute screen pixels. viewportCenter is the
    // canvas center in the same screen coordinates used by drawing and picking.
    [[nodiscard]] MapPoint worldToScreen(MapPoint world, MapPoint viewportCenter) const noexcept;

    // Converts absolute screen pixels back into map units using the same
    // viewportCenter as worldToScreen.
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
