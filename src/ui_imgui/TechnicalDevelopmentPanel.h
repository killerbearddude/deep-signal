#pragma once

// UI-local authoring/selection for P5. It renders owned technical projections
// and submits commands; no hidden candidate truth or raw GameState is accessed.
#include "app/SimulationQueries.h"
#include "app/SimulationService.h"

#include <array>

namespace deep::ui_imgui {
class TechnicalDevelopmentPanel {
  public:
    // Submits the P5 window for this frame using owned query data and commands.
    void render(const SimulationQueries&, SimulationService&, bool& visible);
    // Successful New/Load clears old-world selection and draft typed IDs.
    void resetWorldState();

  private:
    void renderEditor(const SimulationQueries&, SimulationService&);
    void renderProgram(const TechnicalDevelopmentSummary&, SimulationService&);
    TechnicalDevelopmentCharter draft_;
    std::array<char, 128> name_{};
    std::optional<TechnicalDevelopmentProgramId> selected_, editing_;
    std::string notice_ = "Ready";
};
} // namespace deep::ui_imgui
