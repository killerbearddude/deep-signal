#pragma once

// UI-only shell geometry. Kept independent of ImGui so narrow/minimized windows
// and the first-frame menu inset can be checked without a renderer.

#include <algorithm>
#include <cmath>

namespace deep::ui_imgui {

struct ShellRegion {
    float x;
    float y;
    float width;
    float height;
};

struct ShellLayout {
    ShellRegion dock;
    ShellRegion information;

    [[nodiscard]] bool drawable() const noexcept {
        // Public DockSpace auto-sizing has a 4 px minimum. Below it, only keep
        // the docking node alive rather than letting it exceed its reservation.
        return dock.width >= 4.0F && information.width >= 4.0F && dock.height >= 4.0F;
    }
};

// Recover an ordinary floating window without discarding the rest of its saved
// layout. Oversized windows shrink to the available region; valid ones are kept.
[[nodiscard]] inline ShellRegion confineFloatingWindow(ShellRegion window, ShellRegion work) noexcept {
    work.width = std::max(0.0F, work.width);
    work.height = std::max(0.0F, work.height);
    window.width = std::clamp(window.width, 0.0F, work.width);
    window.height = std::clamp(window.height, 0.0F, work.height);
    window.x = std::clamp(window.x, work.x, work.x + work.width - window.width);
    window.y = std::clamp(window.y, work.y, work.y + work.height - window.height);
    return window;
}

[[nodiscard]] inline ShellLayout calculateShellLayout(ShellRegion work, float menuBottom) noexcept {
    // BeginMainMenuBar updates the viewport's work inset for the next frame.
    // Its actual bottom also reserves the menu on the very first frame.
    const float bottom = work.y + std::max(0.0F, work.height);
    work.y = std::clamp(menuBottom, work.y, bottom);
    work.height = bottom - work.y;
    work.width = std::max(0.0F, work.width);

    // Below 600 px the provisional minimum gives way to an equal split, keeping
    // both shell regions valid. User resizing/collapse is intentionally deferred.
    const float preferred = std::clamp(work.width * 0.28F, 300.0F, 420.0F);
    // ImGui rounds window sizes to pixels; round one side before deriving the
    // other so their independent truncation cannot leave a strip at the edge.
    const float informationWidth = std::floor(std::min(preferred, work.width * 0.5F));
    const float dockWidth = work.width - informationWidth;
    return {
        {work.x, work.y, dockWidth, work.height},
        {work.x + dockWidth, work.y, informationWidth, work.height}
    };
}

// Submit a viewport-edge reservation after the global menu. The internal ImGui
// dependency is isolated in ShellLayout.cpp; callers use only plain geometry.
[[nodiscard]] ShellLayout reserveInformationWorkArea(float menuBottom);

} // namespace deep::ui_imgui
