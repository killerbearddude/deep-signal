#pragma once

// Declares the ImGui shipyard production panel.
// The panel reads production rows through SimulationQueries and creates new
// build orders only through SimulationService command execution.

#include "app/SimulationQueries.h"
#include "app/SimulationService.h"

#include <string>
#include <array>
#include <optional>
#include <vector>

namespace deep::ui_imgui {

// Keeps a local editable draft while all design calculations and commits cross
// the query/command boundary. The first colony remains the prototype location.
class ShipyardPanel {
  public:
    void render(const SimulationQueries& queries, SimulationService& service, bool& visible);
    void resetWorldState();

  private:
    void buildSelectedClass(const SimulationQueries& queries, SimulationService& service);
    void renderDesignEditor(const SimulationQueries& queries, SimulationService& service,
                            const std::vector<ShipClassSummary>& classes);
    void selectDraftSource(const ShipClassSummary& source, const std::vector<ShipComponentSummary>& catalog);
    void applyResult(const CommandResult& result);
    void renderOrders(const SimulationQueries& queries, SimulationService& service,
                      const std::vector<ShipClassSummary>& classes);
    void renderOrderDetail(const ProductionBacklogSummary& order,
                           const std::vector<ShipClassSummary>& classes);
    void renderBuildOrder(const SimulationQueries& queries, SimulationService& service,
                          const std::vector<ShipClassSummary>& classes);
    void renderFeedback() const;

    std::optional<ShipyardOrderId> selectedOrderId_;
    std::optional<ShipClassId> selectedBuildClassId_;
    std::optional<ShipClassId> draftSourceId_;
    std::array<char, 128> draftName_{};
    std::vector<int> draftQuantities_;
    // Tabs, row selection and feedback belong only to this UI session.
    bool selectOrdersTab_ = true;
    bool feedbackIsRevision_ = false;
    std::string lastCommandMessage_;
    bool lastCommandSucceeded_ = true;
};

} // namespace deep::ui_imgui
