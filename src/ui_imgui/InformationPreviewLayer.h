#pragma once

// Renders copied interaction records as short-lived native reference windows.
// ImGui keeps live geometry by PreviewId; this component retains no target,
// query DTO, simulation entity, or application state between frames.

#include "app/InformationInteractionState.h"
#include "ui_imgui/ShellLayout.h"

#include <string>

namespace deep {
class InformationInteractionAdapter;
class SimulationQueries;
}

namespace deep::ui_imgui {

// The visible title is fixed and never includes an object name. The hidden ID
// includes both world generation and record number, so retargeting and literal
// ##/### in display names cannot change native window identity.
[[nodiscard]] std::string informationPreviewWindowName(PreviewId id);

class InformationPreviewLayer {
public:
    // previewSnapshot reconciles then copies records. Actions gathered while
    // drawing those records are sent by ID only after the iteration completes.
    void render(const SimulationQueries& queries,
                InformationInteractionAdapter& interactions,
                ShellRegion workArea) const;
};

} // namespace deep::ui_imgui
