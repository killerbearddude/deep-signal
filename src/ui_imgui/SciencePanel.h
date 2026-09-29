#pragma once
// UI-only analysis draft and evidence selection. Only explicit buttons issue
// commands; successful New/Load clears selections through the shell lifecycle.
#include "app/SimulationQueries.h"
#include <array>
namespace deep::ui_imgui {
class SciencePanel {
  public:
    // Draws owned query records and authoring controls, with no raw-state access.
    void render(const SimulationQueries&, SimulationService&, bool& visible);
    // Clears drafts only after a successful world replacement.
    void resetWorldState();

  private:
    AnalysisCharter draft_;
    std::array<char, 128> name_{};
    std::optional<AnalysisProgramId> editing_;
    std::optional<BodyId> body_;
    std::string notice_;
};
} // namespace deep::ui_imgui
