#include "ui_imgui/OperationalWindow.h"

#include <imgui.h>

namespace deep::ui_imgui {
namespace {

// The native UI has one context on one thread. This is frame-local presentation
// configuration, not cached window geometry or an application-state owner.
ShellRegion workArea{};

struct ConstraintContext {
    const char* name;
};

void constrainFloatingWindow(ImGuiSizeCallbackData* data) {
    const auto& context = *static_cast<const ConstraintContext*>(data->UserData);
    const ShellRegion current = confineFloatingWindow(
        {data->Pos.x, data->Pos.y, data->CurrentSize.x, data->CurrentSize.y}, workArea);

    // This callback runs inside Begin, before decorations and clipping rectangles
    // are constructed. Moving after Begin would leave those at the old position.
    // ImGui bypasses these constraints when a window is docked.
    ImGui::SetWindowPos(context.name, {current.x, current.y});

    // A resize keeps its opposite edge fixed. A growing left/top edge lies on
    // that side of the current midpoint; right/bottom and keyboard resizing use
    // the other edge. Shrinking fits either limit. This also covers corner grips.
    const ImVec2 mouse = ImGui::GetMousePos();
    // Double-click auto-fit changes size without moving the opposite corner.
    const bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                           !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    const bool fromLeft = mouseDown && mouse.x < current.x + current.width * 0.5F;
    const bool fromTop = mouseDown && mouse.y < current.y + current.height * 0.5F;
    const float maxWidth = fromLeft ? current.x + current.width - workArea.x
                                   : workArea.x + workArea.width - current.x;
    const float maxHeight = fromTop ? current.y + current.height - workArea.y
                                   : workArea.y + workArea.height - current.y;
    data->DesiredSize.x = std::clamp(data->DesiredSize.x, 1.0F, std::max(1.0F, maxWidth));
    data->DesiredSize.y = std::clamp(data->DesiredSize.y, 1.0F, std::max(1.0F, maxHeight));
}

} // namespace

void setOperationalWorkArea(const ShellRegion work) {
    workArea = work;
}

bool beginOperationalWindow(const char* name, bool* open) {
    // Initial dimensions apply only to windows without saved settings. Existing
    // positions, sizes, collapsed state and docking relationships are retained.
    ImGui::SetNextWindowSize({std::min(600.0F, workArea.width), std::min(400.0F, workArea.height)},
                             ImGuiCond_FirstUseEver);
    ConstraintContext context{name};
    ImGui::SetNextWindowSizeConstraints({1.0F, 1.0F}, {workArea.width, workArea.height},
                                        constrainFloatingWindow, &context);
    return ImGui::Begin(name, open);
}

} // namespace deep::ui_imgui
