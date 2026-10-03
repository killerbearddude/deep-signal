#pragma once

// Read-only Shipyard presentation. The owning panel supplies two independent
// local selections; all design, service and readiness semantics are app queries.

#include "app/SimulationQueries.h"

#include <optional>
#include <vector>

namespace deep::ui_imgui {

void renderShipyardComparison(const SimulationQueries& queries, const std::vector<ShipClassSummary>& classes,
                              std::optional<ShipClassId>& selectionA, std::optional<ShipClassId>& selectionB);

} // namespace deep::ui_imgui
