#include "ui_imgui/ShellLayout.h"

// The shell alone uses ImGui's viewport-side-bar work-area reservation. No
// internal ImGui type is exposed to panels, application services, or simulation.
#include <imgui_internal.h>

namespace deep::ui_imgui {

ShellLayout reserveInformationWorkArea(const float menuBottom) {
    auto* viewport = static_cast<ImGuiViewportP*>(ImGui::GetMainViewport());
    // Use this frame's accumulated menu inset, before reserving the right edge.
    // WorkSize already includes last frame's reservation and would shrink again
    // if passed through the width calculation on every frame.
    const ImRect available = viewport->GetBuildWorkRect();
    const ShellLayout layout = calculateShellLayout(
        {available.Min.x, available.Min.y, available.GetWidth(), available.GetHeight()}, menuBottom);
    if (layout.drawable()) {
        // This transparent, noninteractive shell window only reserves space.
        // The read-only overview is submitted later with fresh selection data.
        // BeginViewportSideBar publishes its work inset for the next NewFrame.
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;
        ImGui::BeginViewportSideBar("##InformationWorkArea", viewport, ImGuiDir_Right,
                                   layout.information.width, flags);
        ImGui::End();
    }
    return layout;
}

} // namespace deep::ui_imgui
