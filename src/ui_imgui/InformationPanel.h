#pragma once

// Responsibility: render the shell's persistent main-selection overview and
// request inspection or Configure only for displayed, world-stamped targets.
// The application owns selection and layout; queries own the projection boundary.
// This component retains no selected ID, DTO, entity, command, or preview state.

#include "app/InformationInteractionAdapter.h"

#include <optional>

struct ImVec2;

namespace deep {
class InformationInteractionAdapter;
class SelectionState;
class SimulationQueries;
}

namespace deep::ui_imgui {

struct InformationPanelFrameResult {
    std::optional<ColonyProcessingOpenIntent> configureProcessing;
};

class InformationPanel {
public:
    // Resolves fresh owned DTOs for this render. Empty or unresolved selection
    // never triggers fallback selection or a gameplay operation.
    [[nodiscard]] InformationPanelFrameResult render(
        const SimulationQueries& queries, const SelectionState& selection,
        InformationInteractionAdapter& interactions, const ImVec2& position,
        const ImVec2& size) const;
};

} // namespace deep::ui_imgui
