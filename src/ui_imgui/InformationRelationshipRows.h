#pragma once

// Draws native relationship rows for both the persistent Information Panel and
// preview windows. Buttons append the displayed, world-scoped reference; the
// caller submits inspections after it finishes rendering its copied UI state.

#include "ui_imgui/InformationRelationships.h"

#include <vector>

namespace deep::ui_imgui {

void renderInformationRelationshipRows(
    const std::vector<InformationRelationship>& relationships,
    std::vector<ObjectReference>& inspectionRequests);

} // namespace deep::ui_imgui
