#pragma once

// Declares the ImGui audit event-log panel.
// The panel reads flattened EventLogEntrySummary DTOs so it does not need to
// inspect SimEvent variants or raw GameState event storage.

#include "app/SimulationQueries.h"

#include <cstddef>

namespace deep::ui_imgui {

// Renders the recent audit trail. Routine economy telemetry is intentionally not
// included because it is no longer part of the player-facing event log.
class EventLogPanel {
public:
    // Draws the newest event summaries and updates visibility when closed.
    void render(const SimulationQueries& queries, bool& visible) const;

private:
    // Presentation limit only. Querying this tail neither prunes durable events
    // nor bounds the simulation's separate daily-economy telemetry.
    static constexpr std::size_t kRecentEventLimit = 25;
};

} // namespace deep::ui_imgui
