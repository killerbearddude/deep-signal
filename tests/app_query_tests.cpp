#include "app/SimulationQueries.h"
#include "app/SimulationService.h"
#include "sim/Commands.h"

// Self-contained regression tests for app-layer read-only query DTOs.
// These tests protect the future UI boundary from drifting back toward direct
// raw GameState vector inspection.

#include <cstdlib>
#include <exception>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

class TestFailure final : public std::runtime_error {
public:
    explicit TestFailure(const std::string_view message)
        : std::runtime_error{std::string{message}} {}
};

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw TestFailure{message};
    }
}

void requireNear(const double actual, const double expected, const std::string_view message) {
    if (std::abs(actual - expected) > 1.0e-6) {
        throw TestFailure{message};
    }
}

void test_colony_summaries_resolve_body_context() {
    // Verifies that colony queries return UI-useful copies with resolved body
    // names. Prevents future panels from needing raw GameState::colonies access.
    const deep::SimulationService service;
    const deep::SimulationQueries queries{service};

    const auto colonies = queries.colonies();

    require(colonies.size() == 1, "home scenario exposes one colony summary");
    require(colonies.front().name == "Terra Directorate", "colony summary includes colony name");
    require(colonies.front().bodyName == "Terra", "colony summary resolves body name");
    require(colonies.front().ownerInstitutionId.has_value(), "colony summary exposes owner institution ID");
    require(colonies.front().ownerInstitutionName == "Strategic Continuity Office",
            "colony summary resolves owner institution name");
    require(colonies.front().mines == 10.0, "colony summary includes mine count");
    require(colonies.front().processorCapacity == 50.0, "colony summary includes processor capacity");
    require(colonies.front().shipyardCapacity == 100.0, "colony summary includes shipyard capacity");
    require(colonies.front().totalRawStockpile > 0.0, "colony summary includes raw stockpile total");
    require(colonies.front().totalProcessedStockpile > 0.0, "colony summary includes processed stockpile total");
}


void test_colony_summaries_include_processing_policy() {
    // Verifies colony queries expose processing allocation state through DTOs so
    // the Colony panel can render controls without reading raw GameState.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;

    require(service.execute(deep::SetColonyProcessingPolicyCommand{
        .colonyId = colonyId,
        .policy = deep::ProcessingPolicy::Manual,
        .manualAllocations = {
            deep::ProcessingAllocation{.material = deep::ProcessedMaterial::Propellant, .weight = 75.0},
            deep::ProcessingAllocation{.material = deep::ProcessedMaterial::ReactorFuel, .weight = 25.0}
        }
    }).ok, "manual processing policy is accepted before colony query");

    const deep::SimulationQueries queries{service};
    const auto colonies = queries.colonies();

    require(colonies.size() == 1, "home scenario still exposes one colony summary");
    require(colonies.front().processingPolicy == deep::ProcessingPolicy::Manual,
            "colony summary exposes processing policy enum");
    require(colonies.front().processingPolicyName == "Manual", "colony summary exposes processing policy display name");
    require(colonies.front().manualProcessingAllocations.size() == deep::processedMaterialCount(),
            "colony summary exposes one manual processing row per processed material");
    require(colonies.front().manualProcessingAllocations.at(deep::processedMaterialIndex(deep::ProcessedMaterial::Propellant)).materialName == "Propellant",
            "manual allocation summary resolves processed material display name");
    requireNear(colonies.front().manualProcessingAllocations.at(deep::processedMaterialIndex(deep::ProcessedMaterial::Propellant)).normalizedPercent,
                75.0,
                "manual allocation summary reports normalized effective percent");
    require(colonies.front().effectiveProcessingAllocations.size() == deep::processedMaterialCount(),
            "colony summary exposes policy-derived processing rows for UI previews");
    requireNear(colonies.front().effectiveProcessingAllocations.at(deep::processedMaterialIndex(deep::ProcessedMaterial::ReactorFuel)).normalizedPercent,
                25.0,
                "manual policy effective allocation is normalized from weights");
    require(colonies.front().processedStockpiles.size() == deep::processedMaterialCount(),
            "colony summary exposes processed stockpiles for policy preview math");
}

void test_shipyard_order_summaries_resolve_names() {
    // Verifies that accepted build orders can be shown without joining colony
    // and ship-class vectors in UI code.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 2
    }).ok, "build order is accepted before querying summaries");

    const deep::SimulationQueries queries{service};
    const auto orders = queries.shipyardOrders();

    require(orders.size() == 1, "one shipyard order summary is returned");
    require(orders.front().colonyName == "Terra Directorate", "order summary resolves colony name");
    require(orders.front().shipClassName == "Survey Cutter", "order summary resolves ship-class name");
    require(orders.front().quantityRequested == 2, "order summary includes requested quantity");
    require(orders.front().quantityCompleted == 0, "new order has no completions");
    require(orders.front().requiredBuildPoints == 500.0, "order summary includes required build points");
    require(orders.front().statusName == "Active", "new order summary reports active status");
}


void test_production_backlog_summaries_expose_queue_eta() {
    // Verifies the UI-facing backlog query includes FIFO queue position and
    // queue-aware ETA so the Shipyard panel does not recalculate production.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "first build order is accepted before backlog query");
    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "second build order is accepted before backlog query");

    const deep::SimulationQueries queries{service};
    const auto backlog = queries.productionBacklog();

    require(backlog.size() == 2, "two production backlog summaries are returned");
    require(backlog.front().colonyName == "Terra Directorate", "backlog summary resolves colony name");
    require(backlog.front().shipClassName == "Survey Cutter", "backlog summary resolves ship class name");
    require(backlog.front().queuePosition == 1, "first backlog row has queue position one");
    require(backlog.at(1).queuePosition == 2, "second backlog row has queue position two");
    require(backlog.front().etaDays.has_value(), "first backlog row has ETA");
    require(backlog.at(1).etaDays.has_value(), "second backlog row has ETA");
    require(*backlog.front().etaDays == 5, "first backlog ETA uses direct capacity");
    require(*backlog.at(1).etaDays == 10, "second backlog ETA includes first order capacity use");
    require(backlog.front().shipsRemaining == 1, "backlog summary exposes ships remaining");
    require(!backlog.front().requiredMaterialsRemaining.empty(), "backlog summary exposes processed material requirements");
    require(backlog.front().requiredMaterialsRemaining.front().material == deep::ProcessedMaterial::StructuralAlloys,
            "first material requirement names structural alloys");
    requireNear(backlog.front().requiredMaterialsRemaining.front().amount, 250.0,
                "first material requirement preserves remaining structural alloy need");
    require(backlog.front().blockingMaterialName.empty(), "well-stocked order has no blocking material name");
    require(backlog.front().statusName == "Building", "well-stocked order reports building status");
}

void test_personnel_summaries_resolve_institution_context() {
    // Personnel queries expose durable person records with resolved institution
    // names so future UI can render staff lists without raw GameState access.
    const deep::SimulationService service;
    const deep::SimulationQueries queries{service};

    const auto personnel = queries.personnel();

    require(personnel.size() >= 4, "home scenario exposes starter personnel summaries");
    require(personnel.front().name == "Director Mara Chen", "personnel summary includes starter person name");
    require(personnel.front().institutionName == "Strategic Continuity Office",
            "personnel summary resolves institution name");
    require(personnel.front().competencies.crisisManagement == 5,
            "personnel summary includes competency values");
    require(personnel.front().seniorityLevel == 5, "personnel summary includes seniority level");
    require(personnel.front().serviceRecord.commendations == 4,
            "personnel summary includes service record counters");
}

void test_ship_class_summaries_expose_build_targets() {
    // Verifies that UI production panels can discover buildable ship classes
    // through query DTOs instead of reading GameState::shipClasses directly.
    const deep::SimulationService service;
    const deep::SimulationQueries queries{service};

    const auto shipClasses = queries.shipClasses();

    require(shipClasses.size() == 1, "home scenario exposes one buildable ship class summary");
    require(shipClasses.front().name == "Survey Cutter", "ship class summary includes Survey Cutter");
    require(shipClasses.front().roleName == "Survey", "ship class summary exposes display role name");
    require(shipClasses.front().buildPoints == 500.0, "ship class summary exposes build points");
}

void test_fleet_summaries_resolve_location_and_order() {
    // Verifies fleet summaries after ship completion and movement assignment.
    // This protects future map/fleet panels from duplicating movement joins.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;
    const deep::BodyId marsId = service.state().bodies.at(1).id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order is accepted before fleet summary test");

    static_cast<void>(service.advanceDays(5));
    const deep::FleetId fleetId = service.state().fleets.front().id;

    require(service.execute(deep::MoveFleetCommand{
        .fleetId = fleetId,
        .destinationBodyId = marsId
    }).ok, "move order is accepted before fleet summary query");

    const deep::SimulationQueries queries{service};
    const auto fleets = queries.fleets();

    require(fleets.size() == 1, "one fleet summary is returned");
    require(fleets.front().currentBodyName == "Terra", "fleet summary resolves current body");
    require(fleets.front().destinationBodyName == "Mars", "fleet summary resolves destination body");
    require(fleets.front().shipCount == 1, "fleet summary includes ship count");
    requireNear(fleets.front().fuelCapacity, 1000.0, "fleet summary includes total fuel capacity");
    requireNear(fleets.front().currentFuel, 760.0, "fleet summary includes fuel after starting movement");
    requireNear(fleets.front().currentRange, 760.0, "fleet summary exposes current fuel range");
    require(fleets.front().activeOrderName == "MoveToBody", "fleet summary includes active order type");
    require(fleets.front().hasActiveOrder, "fleet summary marks active movement orders");
    require(fleets.front().daysRemaining == 5, "fleet summary includes remaining movement days");
    require(fleets.front().activeOrderEtaDays.has_value(), "active movement exposes an ETA");
    require(*fleets.front().activeOrderEtaDays == 5, "active movement ETA uses remaining movement days");
    require(fleets.front().activeOrderProjectedArrivalDay == service.state().date.day + 5,
            "active movement exposes projected arrival day");
    require(fleets.front().totalRouteDurationDays == 5, "single active order route lasts five days");
    require(fleets.front().queuedOrders.empty(), "fleet summary exposes an empty queue for immediate movement");
}

void test_fleet_summaries_include_queued_orders() {
    // Verifies that command panels can display future fleet intent without
    // reading raw Fleet::queuedOrders from GameState.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;
    const deep::BodyId terraId = service.state().bodies.front().id;
    const deep::BodyId marsId = service.state().bodies.at(1).id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order is accepted before queued fleet summary test");

    static_cast<void>(service.advanceDays(5));
    const deep::FleetId fleetId = service.state().fleets.front().id;

    require(service.execute(deep::QueueFleetMoveOrderCommand{
        .fleetId = fleetId,
        .destinationBodyId = marsId
    }).ok, "first queued move starts before queued summary test");
    require(service.execute(deep::QueueFleetMoveOrderCommand{
        .fleetId = fleetId,
        .destinationBodyId = terraId
    }).ok, "follow-up move remains queued before summary test");

    const deep::SimulationQueries queries{service};
    const auto fleet = queries.fleet(fleetId);

    require(fleet.has_value(), "existing fleet ID returns queued fleet summary");
    require(fleet->hasActiveOrder, "summary keeps current order separate from queued orders");
    require(fleet->queuedOrders.size() == 1, "summary includes one queued order");
    require(fleet->queuedOrders.front().queuePosition == 1, "queued order summary has one-based position");
    require(fleet->queuedOrders.front().orderName == "MoveToBody", "queued order summary resolves order name");
    require(fleet->queuedOrders.front().destinationBodyId == terraId, "queued order summary includes destination ID");
    require(fleet->queuedOrders.front().destinationBodyName == "Terra", "queued order summary resolves destination name");
    require(fleet->queuedOrders.front().etaDays == 10,
            "queued order summary exposes cumulative ETA after the active order and queued move");
    require(fleet->queuedOrders.front().projectedStartDay == service.state().date.day + 5,
            "queued order summary exposes projected start after active order arrival");
    require(fleet->queuedOrders.front().projectedArrivalDay == service.state().date.day + 10,
            "queued order summary exposes projected arrival after queued move duration");
    requireNear(fleet->queuedOrders.front().fuelCost, 240.0,
                "queued order summary includes fuel cost from projected origin");
    requireNear(fleet->queuedOrders.front().projectedFuelRemaining, 520.0,
                "queued order summary includes projected fuel after queued move");
    require(fleet->queuedOrders.front().fuelAffordable,
            "queued order summary marks affordable queued movement");
    require(fleet->totalRouteDurationDays == 10, "fleet summary exposes total route duration through the queue");

    const auto preview = queries.fleetMovePreview(fleetId, marsId);
    require(preview.has_value(), "fleet move preview is available for valid fleet and destination");
    requireNear(preview->fuelAvailable, 760.0, "move preview uses current remaining fleet fuel");
    requireNear(preview->queuedFuelRequired, 480.0,
                "move preview accounts for existing queued moves plus the new move");
    requireNear(preview->projectedFuelRemaining, 280.0,
                "move preview exposes remaining fuel after queued route");
    require(preview->canAfford, "move preview marks affordable route as queueable");
}


void test_fleet_summaries_report_empty_timeline_for_idle_fleet() {
    // Verifies that the UI can warn about a fleet with no active or queued
    // orders without inferring state from raw Fleet records.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order is accepted before idle fleet timeline test");

    static_cast<void>(service.advanceDays(5));

    const deep::SimulationQueries queries{service};
    const auto fleet = queries.fleet(service.state().fleets.front().id);

    require(fleet.has_value(), "completed starter ship creates a fleet summary");
    requireNear(fleet->currentFuel, 1000.0, "newly completed fleet starts with full fuel");
    requireNear(fleet->fuelCapacity, 1000.0, "idle fleet summary includes fuel capacity");
    require(!fleet->hasActiveOrder, "new fleet starts idle");
    require(!fleet->activeOrderEtaDays.has_value(), "idle fleet has no active-order ETA");
    require(fleet->totalRouteDurationDays == 0, "idle fleet has zero route duration");
    require(fleet->activeOrderProjectedArrivalDay == service.state().date.day,
            "idle fleet projected arrival defaults to the current day");
    require(fleet->queuedOrders.empty(), "idle fleet has no queued-order timeline rows");
}

void test_single_record_queries_return_matching_summaries() {
    // Verifies the small lookup helpers used by command-oriented UI controls.
    // This prevents panels from rebuilding their own raw GameState lookups.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order is accepted before single-record query test");
    static_cast<void>(service.advanceDays(5));

    const deep::SimulationQueries queries{service};
    const deep::FleetId fleetId = service.state().fleets.front().id;
    const deep::BodyId terraId = service.state().bodies.front().id;

    const auto fleet = queries.fleet(fleetId);
    const auto body = queries.strategicBody(terraId);
    const auto missingFleet = queries.fleet(deep::FleetId{9999});
    const auto missingBody = queries.strategicBody(deep::BodyId{9999});

    require(fleet.has_value(), "existing fleet ID returns a fleet summary");
    require(fleet->id == fleetId, "fleet lookup preserves the requested ID");
    require(fleet->ownerInstitutionName == "Strategic Continuity Office",
            "fleet summary resolves inherited owner institution name");
    require(queries.institutionDisplayName(*fleet->ownerInstitutionId) == "Strategic Continuity Office",
            "institution display-name helper resolves known institution IDs");
    require(queries.institutionDisplayName(deep::InstitutionId{9999}) == "<unknown institution>",
            "institution display-name helper marks unknown institution IDs");
    require(body.has_value(), "existing body ID returns a strategic body summary");
    require(body->name == "Terra", "body lookup resolves Terra");
    require(!missingFleet.has_value(), "missing fleet ID returns no summary");
    require(!missingBody.has_value(), "missing body ID returns no summary");
}


void test_body_system_overview_exposes_counts() {
    // Verifies the Bodies/System panel can show body-level context without
    // scanning raw GameState bodies, colonies, deposits, or fleets in UI code.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order is accepted before body overview query");
    static_cast<void>(service.advanceDays(5));

    const deep::SimulationQueries queries{service};
    const auto bodies = queries.bodySystemOverview();

    require(bodies.size() == 2, "home scenario exposes two body overview rows");
    require(bodies.front().name == "Terra", "first body overview row resolves Terra");
    require(bodies.front().typeName == "Terrestrial", "body overview resolves body type name");
    require(bodies.front().colonyCount == 1, "Terra body overview counts the colony");
    require(bodies.front().mineralDepositCount == 7, "Terra body overview counts mineral deposits");
    require(bodies.front().fleetCount == 1, "Terra body overview counts the newly completed fleet");
    require(bodies.at(1).name == "Mars", "second body overview row resolves Mars");
    require(bodies.at(1).colonyCount == 0, "Mars body overview has no colonies in the home scenario");
    require(bodies.at(1).mineralDepositCount == 4, "Mars body overview counts mineral deposits");
    require(bodies.at(1).fleetCount == 0, "Mars body overview has no fleets before movement");
}

void test_strategic_map_summaries_resolve_positions() {
    // Verifies that the map can draw bodies and fleets from DTOs instead of
    // reading raw GameState body/fleet vectors in UI code.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order is accepted before strategic map query");
    static_cast<void>(service.advanceDays(5));

    const deep::SimulationQueries queries{service};
    const auto bodies = queries.strategicBodies();
    const auto fleets = queries.strategicFleets();

    require(bodies.size() == 2, "home scenario exposes two strategic body summaries");
    require(bodies.front().name == "Terra", "strategic body summary includes body name");
    require(bodies.front().typeName == "Terrestrial", "strategic body summary includes body type name");
    require(bodies.front().x == 0.0 && bodies.front().y == 0.0, "strategic body summary includes coordinates");
    require(bodies.at(1).name == "Mars", "second strategic body summary includes Mars");
    require(bodies.at(1).x == 240.0, "Mars strategic body summary preserves map x coordinate");

    require(fleets.size() == 1, "completed ship creates one strategic fleet summary");
    require(fleets.front().name.find("Survey Cutter Fleet") != std::string::npos, "strategic fleet summary includes fleet name");
    require(fleets.front().x == 0.0 && fleets.front().y == 0.0, "fleet marker resolves current body coordinates");
}

void test_recent_events_returns_limited_chronological_tail() {
    // Verifies that recentEvents(limit) returns the newest audit entries but
    // preserves log order inside that returned window. Routine mining telemetry
    // must not be required for event-log query coverage.
    deep::SimulationService service;
    const deep::ColonyId colonyId = service.state().colonies.front().id;
    const deep::ShipClassId shipClassId = service.state().shipClasses.front().id;
    const deep::BodyId marsId = service.state().bodies.at(1).id;

    require(service.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order is accepted before recent event query");
    static_cast<void>(service.advanceDays(5));
    require(service.execute(deep::MoveFleetCommand{
        .fleetId = service.state().fleets.front().id,
        .destinationBodyId = marsId
    }).ok, "move order is accepted before recent event query");

    const deep::SimulationQueries queries{service};
    const auto allEvents = queries.recentEvents(100);
    const auto recentTwo = queries.recentEvents(2);
    const auto none = queries.recentEvents(0);

    require(allEvents.size() == 3, "setup creates three queryable audit event summaries");
    require(recentTwo.size() == 2, "recentEvents applies the requested limit");
    require(none.empty(), "recentEvents with zero limit is empty");
    require(recentTwo.front().id == allEvents.at(allEvents.size() - 2).id,
            "recentEvents returns the chronological tail window");
    require(recentTwo.front().id.value < recentTwo.back().id.value,
            "recentEvents preserves chronological order within the tail");
    require(recentTwo.back().eventType == "fleet_order_assigned", "event summary exposes flattened event type");
    require(!recentTwo.back().message.empty(), "event summary exposes display message text");
}

} // namespace

int main() {
    try {
        test_colony_summaries_resolve_body_context();
        test_colony_summaries_include_processing_policy();
        test_shipyard_order_summaries_resolve_names();
        test_production_backlog_summaries_expose_queue_eta();
        test_personnel_summaries_resolve_institution_context();
        test_ship_class_summaries_expose_build_targets();
        test_fleet_summaries_resolve_location_and_order();
        test_fleet_summaries_include_queued_orders();
        test_fleet_summaries_report_empty_timeline_for_idle_fleet();
        test_single_record_queries_return_matching_summaries();
        test_body_system_overview_exposes_counts();
        test_strategic_map_summaries_resolve_positions();
        test_recent_events_returns_limited_chronological_tail();
    } catch (const std::exception& ex) {
        std::cerr << "Test failure: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "All Deep Signal app query tests passed.\n";
    return EXIT_SUCCESS;
}
