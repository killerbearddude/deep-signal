#pragma once

// Declares the dedicated fleet orders control panel for the ImGui shell.
// The panel makes movement and cancellation discoverable outside Inspector while
// preserving the command boundary: all reads use SimulationQueries and all
// writes go through SimulationService commands.

#include "app/SelectionState.h"
#include "app/SimulationQueries.h"
#include "app/SimulationService.h"
#include "sim/IdTypes.h"

#include <optional>
#include <string>

namespace deep::ui_imgui {

// Renders fleet movement and cancellation controls for the selected fleet.
// UI-local selection memory allows the operator to pick a fleet, then choose a
// destination body without requiring a fragile multi-selection model.
class FleetOrdersPanel {
public:
    // Draws fleet summary, destination selector, command buttons, and command
    // status. Closing the window updates the shared visibility flag.
    void render(const SimulationQueries& queries,
                SimulationService& service,
                const SelectionState& selection,
                bool& visible);

private:
    // Applies the global map/table selection to this panel's command workflow.
    // Fleet and body selections are remembered independently because the shared
    // SelectionState can represent only one object at a time.
    void syncSelection(const SimulationQueries& queries, const SelectionState& selection);

    // Draws a combo over all known bodies and stores the target destination ID.
    void drawDestinationSelector(const SimulationQueries& queries);

    std::optional<FleetId> selectedFleetId_;
    std::optional<BodyId> destinationBodyId_;
    std::string commandStatus_ = "Ready";
    bool commandSucceeded_ = true;
};

} // namespace deep::ui_imgui
