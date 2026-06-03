#pragma once

// Declares the ImGui colony summary and processing-allocation panel.
// The panel consumes app-layer DTOs for display and sends policy changes through
// SimulationService commands instead of mutating GameState directly.

#include "app/SelectionState.h"
#include "app/SimulationQueries.h"
#include "app/SimulationService.h"
#include "sim/Minerals.h"

#include <array>
#include <optional>
#include <string>

namespace deep::ui_imgui {

// Renders colony overview rows and v1 processor allocation controls.
class ColonyPanel {
public:
    // Draws one table row per colony summary, updates selection when rows are
    // clicked, and submits processing policy edits through the service layer.
    void render(const SimulationQueries& queries,
                SimulationService& service,
                SelectionState& selection,
                bool& visible);

private:
    std::optional<ColonyId> editingColony_;
    ProcessingPolicy selectedPolicy_ = ProcessingPolicy::Balanced;
    std::array<double, processedMaterialCount()> manualWeights_{};
    std::string statusMessage_;

    void loadEditorFromColony(const ColonySummary& colony);
    void normalizeManualWeights() noexcept;
};

} // namespace deep::ui_imgui
