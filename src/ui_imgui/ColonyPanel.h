#pragma once

// Colony directory only: live rows, main selection, and one-shot row reveal.
// Processing drafts and Apply now belong solely to ColonyProcessingEditor.

#include "app/InformationInteractionAdapter.h"
#include "app/SimulationQueries.h"

#include <optional>

namespace deep::ui_imgui {

class ColonyPanel {
public:
    // Focus and scroll to one world-stamped colony row on the next visible render.
    void requestReveal(ObjectReference target);

    // Draws read-only summary rows and routes actual row activation to main selection.
    void render(const SimulationQueries& queries,
                InformationInteractionAdapter& interactions,
                bool& visible);

    // Successful New/Load invalidates any one-shot old-world reveal request.
    void resetWorldState() { pendingReveal_.reset(); }

private:
    friend struct InformationRevealTestAccess;
    std::optional<ObjectReference> pendingReveal_;
};

} // namespace deep::ui_imgui
