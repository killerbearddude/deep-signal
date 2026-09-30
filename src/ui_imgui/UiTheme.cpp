#include "ui_imgui/UiTheme.h"

#include "KarlaFontData.h"

// Keep the palette and geometry together so native review adjustments apply to
// semantic roles rather than introducing unrelated colors in individual panels.

namespace deep::ui_imgui {
namespace {

constexpr ImVec4 rgb(unsigned hex) {
    return {static_cast<float>((hex >> 16U) & 0xffU) / 255.0F,
            static_cast<float>((hex >> 8U) & 0xffU) / 255.0F, static_cast<float>(hex & 0xffU) / 255.0F, 1.0F};
}

ImVec4 withAlpha(UiColor role, float alpha) {
    ImVec4 color = uiColor(role);
    color.w = alpha;
    return color;
}

} // namespace

ImVec4 uiColor(UiColor role) {
    switch (role) {
    case UiColor::Background:
        return rgb(0x0C0F11);
    case UiColor::Panel:
        return rgb(0x14191D);
    case UiColor::Surface:
        return rgb(0x1D252B);
    case UiColor::Hover:
        return rgb(0x27333B);
    case UiColor::Divider:
        return rgb(0x39464F);
    case UiColor::ControlBorder:
        return rgb(0x71818C);
    case UiColor::TextPrimary:
        return rgb(0xE8EEF2);
    case UiColor::TextSecondary:
        return rgb(0xA8B5BF);
    case UiColor::TextMuted:
        return rgb(0x7F909C);
    case UiColor::Focus:
        return rgb(0x69D2E7);
    case UiColor::SelectedSurface:
        return rgb(0x1D2A30);
    case UiColor::Positive:
        return rgb(0x87C99A);
    case UiColor::Warning:
        return rgb(0xE6B566);
    case UiColor::Danger:
        return rgb(0xEE8282);
    }
    return rgb(0xE8EEF2);
}

float uiTextSize(UiTextRole role) {
    switch (role) {
    case UiTextRole::ScreenTitle:
        return 32.0F;
    case UiTextRole::ObjectTitle:
        return 28.0F;
    case UiTextRole::Section:
        return 18.0F;
    case UiTextRole::Body:
        return 20.0F;
    case UiTextRole::Secondary:
        return 17.0F;
    case UiTextRole::Provenance:
        return 14.0F;
    case UiTextRole::Metric:
        return 34.0F;
    case UiTextRole::Tab:
        return 20.0F;
    }
    return 20.0F;
}

void applyDeepSignalTheme() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.Fonts->Fonts.empty()) {
        // The generated array keeps the pinned, unmodified font alive for the
        // atlas lifetime. The atlas must not free this static storage.
        ImFontConfig config;
        config.FontDataOwnedByAtlas = false;
        io.FontDefault = io.Fonts->AddFontFromMemoryTTF(detail::karlaFontData,
                                                        static_cast<int>(sizeof(detail::karlaFontData)),
                                                        uiTextSize(UiTextRole::Body), &config);
        // Preserve the old font's punctuation coverage in existing panels.
        // Karla supplies the primary face; only missing glyphs use this source.
        ImFontConfig fallback;
        fallback.MergeMode = true;
        fallback.SizePixels = uiTextSize(UiTextRole::Body);
        io.Fonts->AddFontDefaultVector(&fallback);
    }

    ImGuiStyle& style = ImGui::GetStyle();
    style.Alpha = 1.0F;
    style.DisabledAlpha = 0.6F;
    // Legacy panels retain their base control sizing while the refined screen
    // opts into the named typography roles. Their layouts have not been reviewed.
    style.FontSizeBase = 16.0F;
    style.WindowPadding = {16.0F, 16.0F};
    style.FramePadding = {8.0F, 5.0F};
    style.ItemSpacing = {10.0F, 8.0F};
    style.ItemInnerSpacing = {6.0F, 4.0F};
    style.CellPadding = {12.0F, 12.0F};
    style.WindowRounding = 0.0F;
    style.ChildRounding = 0.0F;
    style.PopupRounding = 0.0F;
    style.FrameRounding = 1.0F;
    style.TabRounding = 0.0F;
    style.ScrollbarRounding = 0.0F;
    style.GrabRounding = 0.0F;
    style.WindowBorderSize = 1.0F;
    style.ChildBorderSize = 1.0F;
    style.PopupBorderSize = 1.0F;
    style.FrameBorderSize = 1.0F;
    style.TabBorderSize = 0.0F;
    style.TabBarBorderSize = 1.0F;
    style.TabBarOverlineSize = 2.0F;
    style.ScrollbarSize = 14.0F;

    // Assign every slot an opaque neutral base; newly added ImGui color slots
    // cannot unexpectedly inherit the default blue visual language.
    for (ImVec4& color : style.Colors) {
        color = uiColor(UiColor::Surface);
    }
    auto* colors = style.Colors;
    colors[ImGuiCol_Text] = uiColor(UiColor::TextPrimary);
    colors[ImGuiCol_TextDisabled] = uiColor(UiColor::TextMuted);
    colors[ImGuiCol_WindowBg] = uiColor(UiColor::Panel);
    colors[ImGuiCol_ChildBg] = uiColor(UiColor::Panel);
    colors[ImGuiCol_PopupBg] = uiColor(UiColor::Panel);
    colors[ImGuiCol_Border] = uiColor(UiColor::Divider);
    colors[ImGuiCol_BorderShadow] = withAlpha(UiColor::Background, 0.0F);
    colors[ImGuiCol_FrameBg] = uiColor(UiColor::Surface);
    colors[ImGuiCol_FrameBgHovered] = uiColor(UiColor::Hover);
    colors[ImGuiCol_FrameBgActive] = uiColor(UiColor::SelectedSurface);
    colors[ImGuiCol_TitleBg] = uiColor(UiColor::Background);
    colors[ImGuiCol_TitleBgActive] = uiColor(UiColor::Surface);
    colors[ImGuiCol_TitleBgCollapsed] = uiColor(UiColor::Background);
    colors[ImGuiCol_MenuBarBg] = uiColor(UiColor::Background);
    colors[ImGuiCol_ScrollbarBg] = uiColor(UiColor::Panel);
    colors[ImGuiCol_ScrollbarGrab] = uiColor(UiColor::Divider);
    colors[ImGuiCol_ScrollbarGrabHovered] = uiColor(UiColor::ControlBorder);
    colors[ImGuiCol_ScrollbarGrabActive] = uiColor(UiColor::TextSecondary);
    colors[ImGuiCol_CheckMark] = uiColor(UiColor::Focus);
    colors[ImGuiCol_CheckboxSelectedBg] = uiColor(UiColor::SelectedSurface);
    colors[ImGuiCol_SliderGrab] = uiColor(UiColor::ControlBorder);
    colors[ImGuiCol_SliderGrabActive] = uiColor(UiColor::Focus);
    colors[ImGuiCol_Button] = uiColor(UiColor::Surface);
    colors[ImGuiCol_ButtonHovered] = uiColor(UiColor::Hover);
    colors[ImGuiCol_ButtonActive] = uiColor(UiColor::SelectedSurface);
    colors[ImGuiCol_Header] = uiColor(UiColor::Surface);
    colors[ImGuiCol_HeaderHovered] = uiColor(UiColor::Hover);
    colors[ImGuiCol_HeaderActive] = uiColor(UiColor::SelectedSurface);
    colors[ImGuiCol_Separator] = uiColor(UiColor::Divider);
    colors[ImGuiCol_SeparatorHovered] = uiColor(UiColor::ControlBorder);
    colors[ImGuiCol_SeparatorActive] = uiColor(UiColor::Focus);
    colors[ImGuiCol_ResizeGrip] = uiColor(UiColor::Divider);
    colors[ImGuiCol_ResizeGripHovered] = uiColor(UiColor::ControlBorder);
    colors[ImGuiCol_ResizeGripActive] = uiColor(UiColor::Focus);
    colors[ImGuiCol_InputTextCursor] = uiColor(UiColor::Focus);
    colors[ImGuiCol_Tab] = uiColor(UiColor::Surface);
    colors[ImGuiCol_TabHovered] = uiColor(UiColor::Hover);
    colors[ImGuiCol_TabSelected] = uiColor(UiColor::SelectedSurface);
    colors[ImGuiCol_TabSelectedOverline] = uiColor(UiColor::Focus);
    colors[ImGuiCol_TabDimmed] = uiColor(UiColor::Panel);
    colors[ImGuiCol_TabDimmedSelected] = uiColor(UiColor::Surface);
    colors[ImGuiCol_TabDimmedSelectedOverline] = uiColor(UiColor::ControlBorder);
    colors[ImGuiCol_DockingPreview] = withAlpha(UiColor::Focus, 0.25F);
    colors[ImGuiCol_DockingEmptyBg] = uiColor(UiColor::Background);
    colors[ImGuiCol_PlotLines] = uiColor(UiColor::TextSecondary);
    colors[ImGuiCol_PlotLinesHovered] = uiColor(UiColor::Focus);
    colors[ImGuiCol_PlotHistogram] = uiColor(UiColor::ControlBorder);
    colors[ImGuiCol_PlotHistogramHovered] = uiColor(UiColor::Focus);
    colors[ImGuiCol_TableHeaderBg] = uiColor(UiColor::Surface);
    colors[ImGuiCol_TableBorderStrong] = uiColor(UiColor::Divider);
    colors[ImGuiCol_TableBorderLight] = withAlpha(UiColor::Divider, 0.6F);
    colors[ImGuiCol_TableRowBg] = uiColor(UiColor::Panel);
    colors[ImGuiCol_TableRowBgAlt] = uiColor(UiColor::Surface);
    colors[ImGuiCol_TextLink] = uiColor(UiColor::Focus);
    colors[ImGuiCol_TextSelectedBg] = uiColor(UiColor::SelectedSurface);
    colors[ImGuiCol_TreeLines] = uiColor(UiColor::Divider);
    colors[ImGuiCol_DragDropTarget] = uiColor(UiColor::Focus);
    colors[ImGuiCol_DragDropTargetBg] = withAlpha(UiColor::SelectedSurface, 0.75F);
    colors[ImGuiCol_UnsavedMarker] = uiColor(UiColor::TextSecondary);
    colors[ImGuiCol_NavCursor] = uiColor(UiColor::Focus);
    colors[ImGuiCol_NavWindowingHighlight] = uiColor(UiColor::ControlBorder);
    colors[ImGuiCol_NavWindowingDimBg] = withAlpha(UiColor::Background, 0.6F);
    colors[ImGuiCol_ModalWindowDimBg] = withAlpha(UiColor::Background, 0.75F);
}

} // namespace deep::ui_imgui
