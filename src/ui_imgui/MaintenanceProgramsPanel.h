#pragma once
// UI-only standing support draft and selection. Commands cross the service
// boundary; the panel consumes owned queries and never repairs from rendering.
#include "app/SimulationQueries.h"
#include "app/SimulationService.h"
#include <array>
namespace deep::ui_imgui {
class MaintenanceProgramsPanel {
  public:
    void render(const SimulationQueries&, SimulationService&, bool& visible);
    // Called only after successful New/Load, including while this panel is hidden.
    void resetWorldState();

  private:
    void renderEditor(const SimulationQueries&, SimulationService&);
    void renderDetail(const MaintenanceProgramSummary&, const SimulationQueries&, SimulationService&);
    std::array<char, 128> name_{};
    MaintenanceProgramCharter draft_;
    std::optional<MaintenanceProgramId> editing_, selected_;
    std::string notice_ = "Ready";
};
} // namespace deep::ui_imgui
