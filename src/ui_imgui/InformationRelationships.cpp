#include "ui_imgui/InformationRelationships.h"

#include "app/SimulationQueries.h"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace deep::ui_imgui {

namespace {

template <typename IdType>
[[nodiscard]] ObjectReference relatedTarget(const WorldGeneration world, const IdType id) {
    return ObjectReference{world, ObjectTarget{id}};
}

[[nodiscard]] std::vector<InformationRelationship> bodyRelationships(
    const SimulationQueries& queries, const ObjectReference& source, const BodyId sourceId) {
    const auto bodies = queries.bodySystemOverview();
    const auto body = std::find_if(bodies.begin(), bodies.end(), [sourceId](const BodySystemSummary& candidate) {
        return candidate.id == sourceId;
    });
    if (body == bodies.end()) return {};

    std::vector<InformationRelationship> rows;
    if (body->parentBodyId) {
        rows.push_back({RelationshipRole::ParentBody, "RELATIONSHIPS",
                        relatedTarget(source.world, *body->parentBodyId), body->parentBodyName, "Body"});
    }

    // Sorting by native ID keeps order stable when names or daily operational
    // values change. A query row's name never determines its identity.
    auto colonies = queries.colonies();
    std::sort(colonies.begin(), colonies.end(), [](const ColonySummary& a, const ColonySummary& b) {
        return a.id < b.id;
    });
    for (const ColonySummary& colony : colonies) {
        if (colony.bodyId == sourceId) {
            rows.push_back({RelationshipRole::Colony, "COLONIES",
                            relatedTarget(source.world, colony.id), colony.name,
                            "Colony · " + colony.bodyName});
        }
    }

    auto fleets = queries.fleets();
    std::sort(fleets.begin(), fleets.end(), [](const FleetSummary& a, const FleetSummary& b) {
        return a.id < b.id;
    });
    for (const FleetSummary& fleet : fleets) {
        if (fleet.currentBodyId == sourceId && !fleet.hasActiveOrder) {
            rows.push_back({RelationshipRole::StationedFleet, "STATIONED FLEETS",
                            relatedTarget(source.world, fleet.id), fleet.name, "Fleet"});
        }
    }
    return rows;
}

[[nodiscard]] std::vector<InformationRelationship> colonyRelationships(
    const SimulationQueries& queries, const ObjectReference& source, const ColonyId sourceId) {
    const auto colonies = queries.colonies();
    const auto colony = std::find_if(colonies.begin(), colonies.end(), [sourceId](const ColonySummary& candidate) {
        return candidate.id == sourceId;
    });
    if (colony == colonies.end()) return {};

    return {{RelationshipRole::Body, "RELATIONSHIPS",
             relatedTarget(source.world, colony->bodyId), colony->bodyName, "Body"}};
}

[[nodiscard]] std::vector<InformationRelationship> fleetRelationships(
    const SimulationQueries& queries, const ObjectReference& source, const FleetId sourceId) {
    const auto fleet = queries.fleet(sourceId);
    if (!fleet) return {};

    const RelationshipRole currentRole = fleet->hasActiveOrder
        ? RelationshipRole::DepartureBody : RelationshipRole::CurrentBody;
    std::vector<InformationRelationship> rows{
        {currentRole, "RELATIONSHIPS", relatedTarget(source.world, fleet->currentBodyId),
         fleet->currentBodyName, "Body"}
    };
    if (fleet->hasActiveOrder && fleet->destinationBodyId) {
        rows.push_back({RelationshipRole::DestinationBody, "RELATIONSHIPS",
                        relatedTarget(source.world, *fleet->destinationBodyId),
                        fleet->destinationBodyName, "Body"});
    }
    return rows;
}

} // namespace

std::vector<InformationRelationship> informationRelationships(
    const SimulationQueries& queries, const ObjectReference source) {
    return std::visit([&](const auto id) {
        using IdType = std::decay_t<decltype(id)>;
        if constexpr (std::is_same_v<IdType, BodyId>) {
            return bodyRelationships(queries, source, id);
        } else if constexpr (std::is_same_v<IdType, ColonyId>) {
            return colonyRelationships(queries, source, id);
        } else {
            return fleetRelationships(queries, source, id);
        }
    }, source.object);
}

} // namespace deep::ui_imgui
