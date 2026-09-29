#pragma once

// Compact authoring and execution view for durable home-supported survey work.
// Only the draft and visible selection live here; commands own all world changes.

#include "app/SimulationQueries.h"
#include "app/SimulationService.h"

#include <array>
#include <optional>
#include <string>

namespace deep::ui_imgui {

class SurveyProgramsPanel {
public:
    void render(const SimulationQueries& queries, SimulationService& service, bool& visible);
    void resetWorldState();

private:
    void renderEditor(const SimulationQueries& queries, SimulationService& service);
    void renderProgramDetail(const SurveyProgramSummary& program, const SimulationQueries& queries,
                             SimulationService& service);
    void applyResult(const CommandResult& result);
    void editProgram(const SurveyProgramSummary& program);
    [[nodiscard]] SurveyProgramCharter currentCharter() const;

    std::array<char, 128> draftName_{};
    SurveyProgramCharter draft_;
    std::optional<SurveyProgramId> selectedProgramId_;
    std::optional<SurveyProgramId> editingProgramId_;
    std::string notice_ = "Ready";
    bool lastActionSucceeded_ = true;
};

} // namespace deep::ui_imgui
