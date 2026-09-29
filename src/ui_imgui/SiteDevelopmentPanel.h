#pragma once

// Local drafts for site registration, frozen construction packages and durable
// operating policy. Rendering consumes owned queries and commands only.
#include "app/SimulationQueries.h"
#include "app/SimulationService.h"
#include <array>
namespace deep::ui_imgui {
class SiteDevelopmentPanel {
  public:
    // Renders authoring and retained physical obligations; no work occurs in UI.
    void render(const SimulationQueries&, SimulationService&, bool& visible);
    // Successful New/Load discards every old-world selection and typed draft ID.
    void resetWorldState();

  private:
    void renderEditor(const SimulationQueries&, SimulationService&);
    void renderDevelopment(const SiteDevelopmentSummary&, const SimulationQueries&, SimulationService&);
    void renderSite(const SiteSummary&, const SimulationQueries&, SimulationService&);
    void renderRelatedFreight(const std::vector<FreightProgramId>&, const SimulationQueries&,
                              SimulationService&);
    CreateSiteDevelopmentCommand draft_;
    std::array<char, 128> name_{}, siteName_{};
    std::array<int, siteModuleKindCount> quantities_{1, 1, 1, 1, 1};
    bool registerSite_ = true;
    std::optional<SiteDevelopmentProgramId> selectedDevelopment_, editingDevelopment_;
    std::optional<SiteId> selectedSite_, editingOperation_;
    SiteOperatingPolicy operationDraft_;
    std::string notice_ = "Ready";
};
} // namespace deep::ui_imgui
