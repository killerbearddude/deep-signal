#pragma once

// Semantic presentation colors and the desktop ImGui style. This layer owns
// appearance only; callers choose roles from already-projected application data.

#include <imgui.h>

namespace deep::ui_imgui {

enum class UiColor {
    Background,
    Panel,
    Surface,
    Hover,
    Divider,
    ControlBorder,
    TextPrimary,
    TextSecondary,
    TextMuted,
    Focus,
    SelectedSurface,
    Positive,
    Warning,
    Danger
};

ImVec4 uiColor(UiColor role);

// Apply once after creating the ImGui context. Panels share these neutral
// surfaces and use accent/status colors only for their corresponding roles.
void applyDeepSignalTheme();

} // namespace deep::ui_imgui
