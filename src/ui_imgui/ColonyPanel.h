#pragma once

// Declares the ImGui colony summary and processing-allocation panel.
// The panel consumes app-layer DTOs for display and sends policy changes through
// SimulationService commands instead of mutating GameState directly.

#include "app/InformationInteractionAdapter.h"
#include "app/SimulationQueries.h"
#include "app/SimulationService.h"
#include "sim/Minerals.h"

#include <array>
#include <optional>
#include <string>

namespace deep::ui_imgui {

// Renders colony overview rows and processor allocation controls. The policy and
// weights are an unapplied UI draft until Apply submits a command; queries remain
// the source of authoritative colony values.
class ColonyPanel {
public:
    // Draws one table row per colony summary, updates selection when rows are
    // clicked, and submits processing policy edits through the service layer.
    void render(const SimulationQueries& queries,
                SimulationService& service,
                InformationInteractionAdapter& interactions,
                bool& visible);

    // Discard only this editor's old-world target/draft/status on success. A
    // failed Load never calls this; ordinary selection keeps existing behavior.
    void resetWorldState() {
        editingColony_.reset();
        selectedPolicy_ = ProcessingPolicy::Balanced;
        manualWeights_.fill(0.0);
        statusMessage_.clear();
    }

private:
    friend struct InformationLifecycleTestAccess;
    // Identity only: never retain pointers into a query result across frames.
    std::optional<ColonyId> editingColony_;
    ProcessingPolicy selectedPolicy_ = ProcessingPolicy::Balanced;
    // Dimensionless relative weights, not percentages or material quantities.
    std::array<double, processedMaterialCount()> manualWeights_{};
    std::string statusMessage_;

    // Replaces the local draft from a copied DTO. A successful Apply invalidates
    // editingColony_ so the next render reloads the accepted service state.
    void loadEditorFromColony(const ColonySummary& colony);
    // Normalizes nonnegative weights when their total exceeds the comparison
    // epsilon; otherwise uses equal weights as a usable Manual starting point.
    void normalizeManualWeights() noexcept;
};

} // namespace deep::ui_imgui
