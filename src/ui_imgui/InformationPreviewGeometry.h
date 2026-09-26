#pragma once

// Initial placement only. ImGui owns each preview's live rectangle after its
// first appearance; moving, resizing, and retargeting never update this model.

#include "app/InformationInteractionState.h"
#include "ui_imgui/ShellLayout.h"

#include <algorithm>

namespace deep::ui_imgui {

[[nodiscard]] inline ShellRegion initialPreviewGeometry(const PreviewId id, const ShellRegion work) noexcept {
    constexpr float width = 310.0F;
    constexpr float height = 340.0F;
    constexpr float inset = 16.0F;
    constexpr float stagger = 28.0F;
    // Adjacent identities start apart without moving any window already open.
    // The placement may repeat eventually; there is no cap on preview records.
    const float offset = static_cast<float>((id.value == 0 ? 0 : id.value - 1) % 8) * stagger;
    const float fittedWidth = std::min(width, std::max(0.0F, work.width));
    const float fittedHeight = std::min(height, std::max(0.0F, work.height));
    return confineFloatingWindow(
        {work.x + work.width - fittedWidth - inset - offset,
         work.y + inset + offset, fittedWidth, fittedHeight}, work);
}

} // namespace deep::ui_imgui
