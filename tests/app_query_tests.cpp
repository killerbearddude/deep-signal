#include "app/SimulationQueries.h"
#include "app/SimulationService.h"
#include "sim/Commands.h"

// Self-contained regression tests for app-layer read-only query DTOs.
// These tests protect the future UI boundary from drifting back toward direct
// raw GameState vector inspection.

#include <cstdlib>
#include <exception>
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

void test_colony_summaries_resolve_body_context() {
    // Verifies that colony queries return UI-useful copies with resolved body
    // names. Prevents future panels from needing raw GameState::colonies access.
    const deep::SimulationService service;
    const deep::SimulationQueries queries{service};

    const auto colonies = queries.colonies();

    require(colonies.size() == 1, "home scenario exposes one colony summary");
    require(colonies.front().name == "Terra Directorate", "colony summary includes colony name");
    require(colonies.front().bodyName == "Terra", "colony summary resolves body name");
    require(colonies.front().mines == 10.0, "colony summary includes mine count");
    require(colonies.front().processorCapacity == 50.0, "colony summary includes processor capacity");
    require(colonies.front().shipyardCapacity == 100.0, "colony summary includes shipyard capacity");
    require(colonies.front().totalRawStockpile > 0.0, "colony summary includes raw stockpile total");
    require(colonies.front().totalProcessedStockpile > 0.0, "colony summary includes processed stockpile total");
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
    require(backlog.front().blockingMaterialName.empty(), "well-stocked order has no blocking material name");
    require(backlog.front().statusName == "Active", "well-stocked order remains active");
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
    require(fleets.front().activeOrderName == "MoveToBody", "fleet summary includes active order type");
    require(fleets.front().hasActiveOrder, "fleet summary marks active movement orders");
    require(fleets.front().daysRemaining == 5, "fleet summary includes remaining movement days");
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
        test_shipyard_order_summaries_resolve_names();
        test_production_backlog_summaries_expose_queue_eta();
        test_ship_class_summaries_expose_build_targets();
        test_fleet_summaries_resolve_location_and_order();
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
