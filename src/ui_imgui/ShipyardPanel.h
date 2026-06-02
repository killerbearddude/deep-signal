#pragma once

// Declares the ImGui shipyard production panel.
// The panel reads production rows through SimulationQueries and creates new
// build orders only through SimulationService command execution.

#include "app/SimulationQueries.h"
#include "app/SimulationService.h"

#include <string>

namespace deep::ui_imgui {

// Renders the first production control surface for Prototype 0.1.
// It intentionally supports only the fixed Survey Cutter order until a future
// ship designer and production queue editor exist.
class ShipyardPanel {
public:
    // Draws current shipyard orders and a one-click Survey Cutter build action.
    void render(const SimulationQueries& queries, SimulationService& service);

private:
    void buildSurveyCutter(const SimulationQueries& queries, SimulationService& service);
    void applyResult(const CommandResult& result);

    std::string lastCommandMessage_ = "Ready";
    bool lastCommandSucceeded_ = true;
};

} // namespace deep::ui_imgui
