#include "app/InformationInteractionAdapter.h"
#include "app/SimulationQueries.h"
#include "sim/Commands.h"
#include "sim/ScenarioFactory.h"
#include "ui_imgui/InformationRelationships.h"

// Exercise the UI-only projection through real native DTOs and commands. The
// projection creates no preview; inspection and validation remain in the adapter.

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace deep;
using namespace deep::ui_imgui;

void require(const bool condition, const std::string_view message) {
    if (!condition) throw std::runtime_error{std::string{message}};
}

template <typename IdType>
void requireTarget(const InformationRelationship& row, const WorldGeneration world,
                   const IdType id, const std::string_view message) {
    require(row.target.world == world && std::holds_alternative<IdType>(row.target.object)
            && std::get<IdType>(row.target.object) == id, message);
}

[[nodiscard]] BodyId bodyIdByName(const GameState& state, const std::string_view name) {
    const auto found = std::find_if(state.bodies.begin(), state.bodies.end(), [name](const Body& body) {
        return body.name == name;
    });
    require(found != state.bodies.end(), "required body exists");
    return found->id;
}

[[nodiscard]] GameState relationshipWorld() {
    auto state = createHomeSystemScenario();
    const BodyId mars = bodyIdByName(state, "Mars");
    state.colonies.at(1).name = "Mars ##Naval ###Yards";

    // Add two more Mars colonies, including one inserted out of ID order, to
    // cover the A/B/C inspection scenario without changing the starter world.
    Colony another = state.colonies.at(1);
    another.id = ColonyId{state.ids.nextColonyId++};
    another.name = "Ares Colony";
    state.colonies.insert(state.colonies.begin(), another);
    Colony third = state.colonies.at(2);
    third.id = ColonyId{state.ids.nextColonyId++};
    third.name = "Mars Research Annex";
    state.colonies.push_back(third);

    const auto& shipClass = state.shipClasses.front();
    for (const std::string_view name : {"Idle ##Fleet ###One", "Moving Fleet"}) {
        const FleetId fleet{state.ids.nextFleetId++};
        const ShipId ship{state.ids.nextShipId++};
        state.fleets.push_back(Fleet{
            .id = fleet, .name = std::string{name}, .currentBodyId = mars,
            .destinationBodyId = std::nullopt, .shipIds = {ship}, .activeOrder = {},
            .queuedOrders = {}, .ownerInstitutionId = std::nullopt
        });
        state.ships.push_back(Ship{
            .id = ship, .shipClassId = shipClass.id, .name = "Relationship test ship",
            .fleetId = fleet, .fuel = shipClass.fuelCapacity
        });
    }
    return state;
}

void body_and_colony_relationships() {
    SimulationService service{relationshipWorld()};
    SimulationQueries queries{service};
    const BodyId mars = bodyIdByName(service.state(), "Mars");
    const BodyId sun = bodyIdByName(service.state(), "Sun");
    const auto& originalColony = service.state().colonies.at(2);
    const auto& addedColony = service.state().colonies.front();
    const auto& thirdColony = service.state().colonies.back();
    const WorldGeneration world{37};

    const auto rows = informationRelationships(queries, {world, mars});
    require(rows.size() == 6, "Mars has parent, three colonies, and two idle fleets");
    require(rows.at(0).role == RelationshipRole::ParentBody
            && rows.at(0).section == "RELATIONSHIPS" && rows.at(0).name == "Sun",
            "parent body leads the relationship list");
    requireTarget(rows.at(0), world, sun, "parent target is typed and world stamped");
    require(rows.at(1).role == RelationshipRole::Colony
            && rows.at(1).section == "COLONIES"
            && rows.at(1).name == "Mars ##Naval ###Yards",
            "literal ImGui punctuation remains in owned display name");
    requireTarget(rows.at(1), world, originalColony.id, "first colony follows stable ID order");
    require(rows.at(2).name == "Ares Colony" && rows.at(2).typeLabel == "Colony · Mars",
            "second colony retains body context and stable native order");
    requireTarget(rows.at(2), world, addedColony.id, "second colony carries typed ID");
    require(rows.at(3).name == "Mars Research Annex", "third colony follows stable ID order");
    requireTarget(rows.at(3), world, thirdColony.id, "third colony carries typed ID");
    require(rows.at(4).role == RelationshipRole::StationedFleet
            && rows.at(4).section == "STATIONED FLEETS"
            && rows.at(4).name == "Idle ##Fleet ###One", "stationed fleet follows colonies");
    require(rows.at(5).role == RelationshipRole::StationedFleet
            && rows.at(5).name == "Moving Fleet", "second idle fleet follows native ID order");

    const auto colonyRows = informationRelationships(queries, {world, originalColony.id});
    require(colonyRows.size() == 1 && colonyRows.front().role == RelationshipRole::Body
            && colonyRows.front().name == "Mars" && colonyRows.front().typeLabel == "Body",
            "colony projects its native body relationship");
    requireTarget(colonyRows.front(), world, mars, "colony body is typed and world stamped");
    require(informationRelationships(queries, {world, sun}).empty(),
            "root body with no children has no empty relationship sections");
    require(informationRelationships(queries, {world, BodyId{99999}}).empty(),
            "missing body emits no rows");
    require(informationRelationships(queries, {world, ColonyId{99999}}).empty(),
            "missing colony emits no rows");
    require(informationRelationships(queries, {world, FleetId{99999}}).empty(),
            "missing fleet emits no rows");
}

void moving_fleet_is_a_departure_reference_not_stationed() {
    SimulationService service{relationshipWorld()};
    SimulationQueries queries{service};
    const BodyId mars = bodyIdByName(service.state(), "Mars");
    const BodyId terra = bodyIdByName(service.state(), "Terra");
    const FleetId idle = service.state().fleets.at(0).id;
    const FleetId moving = service.state().fleets.at(1).id;
    const WorldGeneration world{73};

    const auto idleRows = informationRelationships(queries, {world, idle});
    require(idleRows.size() == 1 && idleRows.front().role == RelationshipRole::CurrentBody,
            "idle fleet has current body only");
    requireTarget(idleRows.front(), world, mars, "idle current body is Mars");

    require(service.execute(MoveFleetCommand{.fleetId = moving, .destinationBodyId = terra}).ok,
            "real movement command starts fleet transit");
    const auto movingSummary = queries.fleet(moving);
    require(movingSummary && movingSummary->hasActiveOrder
            && movingSummary->currentBodyId == mars && movingSummary->destinationBodyId == terra,
            "moving DTO retains departure body reference and destination");
    const auto overview = queries.bodySystemOverview();
    const auto marsOverview = std::find_if(overview.begin(), overview.end(), [mars](const BodySystemSummary& body) {
        return body.id == mars;
    });
    require(marsOverview != overview.end() && marsOverview->fleetCount == 2,
            "body overview count includes moving departure reference");

    const auto bodyRows = informationRelationships(queries, {world, mars});
    require(bodyRows.size() == 5, "moving fleet leaves stationed list while parent/colonies remain");
    requireTarget(bodyRows.back(), world, idle, "only idle fleet is stationed at Mars");
    require(bodyRows.back().role == RelationshipRole::StationedFleet,
            "stationed section uses explicit idle rule");

    const auto movingRows = informationRelationships(queries, {world, moving});
    require(movingRows.size() == 2 && movingRows.at(0).role == RelationshipRole::DepartureBody
            && movingRows.at(1).role == RelationshipRole::DestinationBody,
            "moving fleet exposes departure and active destination in order");
    requireTarget(movingRows.at(0), world, mars, "departure uses retained current body ID");
    requireTarget(movingRows.at(1), world, terra, "destination uses active destination ID");

    require(service.execute(QueueFleetMoveOrderCommand{.fleetId = moving, .destinationBodyId = mars}).ok,
            "real queue command adds a future leg");
    const auto queuedRows = informationRelationships(queries, {world, moving});
    require(queuedRows.size() == 2, "queued destination does not become a relationship row");
    requireTarget(queuedRows.at(1), world, terra, "active destination remains Terra despite queue");
}

void stale_world_relationship_is_rejected() {
    SimulationService service{relationshipWorld()};
    SimulationQueries queries{service};
    InformationInteractionAdapter adapter{service};
    const BodyId mars = bodyIdByName(service.state(), "Mars");
    const auto rows = informationRelationships(queries, {adapter.world(), mars});
    require(rows.size() > 1, "displayed Mars rows exist before replacement");
    const ObjectReference oldRelationship = rows.at(1).target;
    // Use the production replacement path, which advances the interaction
    // generation as well as replacing the service's world.
    require(adapter.newGame().ok, "adapter advances world generation");
    require(adapter.world() != oldRelationship.world,
            "relationship remembers the world under which it was projected");
    require(!adapter.inspect(oldRelationship), "old relationship click cannot attach to reused ID");
    require(adapter.previewSnapshot().empty(), "rejected stale click creates no preview");
}

void relationship_inspection_keeps_main_and_pin_independent() {
    SimulationService service{relationshipWorld()};
    SimulationQueries queries{service};
    InformationInteractionAdapter adapter{service};
    const BodyId mars = bodyIdByName(service.state(), "Mars");
    require(adapter.select({adapter.world(), mars}), "select Mars as main context");
    const auto rows = informationRelationships(queries, {adapter.world(), mars});
    std::vector<InformationRelationship> colonies;
    for (const auto& row : rows) {
        if (row.role == RelationshipRole::Colony) colonies.push_back(row);
    }
    require(colonies.size() == 3, "Mars displays three test colony relationships");

    const auto first = adapter.inspect(colonies.at(0).target);
    require(first.has_value() && adapter.pin(*first), "inspect then explicitly pin colony A");
    const auto temporary = adapter.inspect(colonies.at(1).target);
    require(temporary.has_value() && *temporary != *first, "colony B gets a new temporary preview");
    require(adapter.inspect(colonies.at(2).target) == temporary,
            "colony C reuses B's temporary PreviewId");
    auto previews = adapter.previewSnapshot();
    require(previews.size() == 2 && previews.at(0).id == *first && previews.at(0).pinned
            && previews.at(0).target == colonies.at(0).target
            && previews.at(1).id == *temporary && !previews.at(1).pinned
            && previews.at(1).target == colonies.at(2).target,
            "pinned A stays fixed while the temporary record displays C");
    require(adapter.mainSelection().isBodySelected(mars),
            "relationship inspection never changes the main Mars selection");

    const auto fromPin = informationRelationships(queries, colonies.at(0).target);
    require(fromPin.size() == 1 && fromPin.front().role == RelationshipRole::Body,
            "pinned colony offers its body relationship");
    require(adapter.inspect(fromPin.front().target) == temporary,
            "Inspect Mars from pinned A retargets the shared temporary");
    previews = adapter.previewSnapshot();
    require(previews.at(0).id == *first && previews.at(0).target == colonies.at(0).target
            && previews.at(1).id == *temporary && previews.at(1).target == fromPin.front().target
            && adapter.mainSelection().isBodySelected(mars),
            "pin and main context survive inspection from a pin");

    const auto parent = informationRelationships(queries, fromPin.front().target).front();
    require(parent.role == RelationshipRole::ParentBody && adapter.inspect(parent.target) == temporary,
            "Inspect inside temporary Mars reuses its PreviewId for the parent body");
    require(adapter.mainSelection().isBodySelected(mars), "temporary inspection leaves main context unchanged");
}

void disappeared_relationship_target_is_rejected() {
    SimulationService service{relationshipWorld()};
    SimulationQueries queries{service};
    InformationInteractionAdapter adapter{service};
    const BodyId mars = bodyIdByName(service.state(), "Mars");
    const auto rows = informationRelationships(queries, {adapter.world(), mars});
    const auto colony = std::find_if(rows.begin(), rows.end(), [](const InformationRelationship& row) {
        return row.role == RelationshipRole::Colony;
    });
    require(colony != rows.end(), "displayed relationship exists before target disappears");
    auto changed = service.state();
    const ColonyId gone = std::get<ColonyId>(colony->target.object);
    std::erase_if(changed.colonies, [gone](const Colony& row) { return row.id == gone; });
    service = SimulationService{std::move(changed)};
    require(!adapter.inspect(colony->target) && adapter.previewSnapshot().empty(),
            "inspection rejects a missing target without creating a fallback preview");
    const auto updated = informationRelationships(queries, {adapter.world(), mars});
    require(std::none_of(updated.begin(), updated.end(), [gone](const InformationRelationship& row) {
        return std::holds_alternative<ColonyId>(row.target.object)
            && std::get<ColonyId>(row.target.object) == gone;
    }), "next projection naturally removes the disappeared relationship row");
}

} // namespace

int main() {
    try {
        body_and_colony_relationships();
        std::cout << "PASS native body and colony relationships\n";
        moving_fleet_is_a_departure_reference_not_stationed();
        std::cout << "PASS idle and moving fleet relationship semantics\n";
        stale_world_relationship_is_rejected();
        std::cout << "PASS stale world relationship rejection\n";
        relationship_inspection_keeps_main_and_pin_independent();
        std::cout << "PASS Mars A/B/C and preview relationship inspection\n";
        disappeared_relationship_target_is_rejected();
        std::cout << "PASS disappeared relationship target rejection\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Information relationship test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
