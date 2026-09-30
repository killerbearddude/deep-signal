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

// Unscaled font sizes. ImGui applies the current display scale after a role is
// selected, so callers should not multiply these by GetFontSize().
enum class UiTextRole { ScreenTitle, ObjectTitle, Section, Body, Secondary, Provenance, Metric, Tab };

float uiTextSize(UiTextRole role);

// Apply once after creating the ImGui context. Panels share these neutral
// surfaces and the embedded font, independent of runtime paths or OS fonts.
void applyDeepSignalTheme();

} // namespace deep::ui_imgui
