#pragma once

// Compact freight authoring and custody inspection. Drafts and selection belong
// to the application; every world change crosses SimulationService commands.

#include "app/SimulationQueries.h"
#include "app/SimulationService.h"

#include <array>
#include <optional>
#include <string>

namespace deep::ui_imgui {

class FreightProgramsPanel {
public:
    // Renders owned live projections and handles only explicit widget actions.
    void render(const SimulationQueries&, SimulationService&, bool& visible);
    // Called by the shell only after successful world replacement, including
    // while hidden. Failed Load must retain this draft and visible selection.
    void resetWorldState();

private:
    void renderEditor(const SimulationQueries&, SimulationService&);
    void renderDetail(const FreightProgramSummary&, SimulationService&);
    void editProgram(const FreightProgramSummary&);
    void applyResult(const CommandResult&);
    [[nodiscard]] FreightProgramCharter currentCharter() const;

    std::array<char, 128> draftName_{};
    FreightProgramCharter draft_;
    std::optional<FreightProgramId> selectedProgramId_;
    std::optional<FreightProgramId> editingProgramId_;
    std::string notice_ = "Ready";
    bool lastActionSucceeded_ = true;
};

} // namespace deep::ui_imgui
