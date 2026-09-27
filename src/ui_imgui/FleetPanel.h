#pragma once

// Declares the ImGui fleet summary panel.
// Fleet rows are read from SimulationQueries so the UI cannot mutate or retain
// references into GameState vectors.

#include "app/InformationInteractionAdapter.h"
#include "app/SimulationQueries.h"

#include <optional>

namespace deep::ui_imgui {

// Renders fleet location, ship count, and active-order state.
class FleetPanel {
public:
    // Focus and scroll to one world-stamped fleet row on the next visible render.
    void requestReveal(ObjectReference target);

    // Draws one table row per fleet summary and updates visibility when closed.
    void render(const SimulationQueries& queries, InformationInteractionAdapter& interactions, bool& visible);

private:
    friend struct InformationRevealTestAccess;
    std::optional<ObjectReference> pendingReveal_;
};

} // namespace deep::ui_imgui
