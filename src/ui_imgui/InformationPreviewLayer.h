#pragma once

// Renders copied interaction records as short-lived native reference windows.
// ImGui keeps live geometry by PreviewId; this component retains no target,
// query DTO, simulation entity, or application state between frames.

#include "app/InformationInteractionAdapter.h"
#include "ui_imgui/ShellLayout.h"

#include <optional>
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

// The inner scroll region follows the displayed typed target. Retargeting a
// stable temporary window creates fresh content at the top; live refresh of the
// same target retains its scroll without caching target state in this layer.
[[nodiscard]] std::string informationPreviewContentName(ObjectReference target);

// Captured from the resolved content and its originating window at activation.
// Navigation and any preview disposition are decided by the app after rendering.
struct PreviewGoToIntent {
    PreviewId sourcePreviewId;
    ObjectReference displayedTarget;
};

struct InformationPreviewFrameResult {
    std::optional<PreviewGoToIntent> goTo;
    std::optional<ColonyProcessingOpenIntent> configureProcessing;
};

class InformationPreviewLayer {
public:
    // previewSnapshot reconciles then copies records. Actions gathered while
    // drawing those records are sent by ID only after the iteration completes.
    // A Go To click returns the displayed world-scoped target and source ID;
    // this layer never changes main selection or workspace context.
    [[nodiscard]] InformationPreviewFrameResult render(
        const SimulationQueries& queries,
        InformationInteractionAdapter& interactions,
        ShellRegion workArea) const;
};

} // namespace deep::ui_imgui
