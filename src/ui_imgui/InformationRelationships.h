#pragma once

// Read-only, owned relationship rows for native information surfaces. Targets
// carry the generation of the displayed source; names are presentation only.

#include "app/InformationInteractionState.h"

#include <string>
#include <vector>

namespace deep {
class SimulationQueries;
}

namespace deep::ui_imgui {

enum class RelationshipRole {
    ParentBody,
    Body,
    Colony,
    StationedFleet,
    CurrentBody,
    DepartureBody,
    DestinationBody
};

struct InformationRelationship {
    RelationshipRole role;
    std::string section;
    ObjectReference target;
    std::string name;
    std::string typeLabel;
};

// Resolve current native DTOs for a displayed object. A missing source produces
// no rows. Query failures propagate, and inspection remains the adapter's job.
[[nodiscard]] std::vector<InformationRelationship> informationRelationships(
    const SimulationQueries& queries, ObjectReference source);

} // namespace deep::ui_imgui
