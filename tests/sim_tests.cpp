#include "sim/Commands.h"
#include "sim/Events.h"
#include "sim/Minerals.h"
#include "sim/ScenarioFactory.h"
#include "sim/Simulation.h"

// Self-contained regression tests for the headless simulation layer.
// These tests avoid third-party dependencies for Phase 1, while still exercising
// the command API, daily tick order, economy, production, movement, and event log.

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

// Exception type used by the tiny test harness. Throwing instead of calling
// std::exit inside assertions keeps failures visible through one catch path.
class TestFailure final : public std::runtime_error {
public:
    explicit TestFailure(const std::string_view message)
        : std::runtime_error{std::string{message}} {}
};

// Minimal assertion helper. The message describes the expected behavior rather
// than restating the expression so failures remain readable in CTest output.
void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw TestFailure{message};
    }
}

void test_time_advancement() {
    // Verifies that the day counter advances deterministically while routine
    // mining is recorded as telemetry instead of audit events. This prevents a
    // regression where daily extraction floods the player-facing event log.
    deep::Simulation sim{deep::createHomeSystemScenario()};
    require(sim.state().date.day == 0, "new simulation starts at day 0");

    const auto events = sim.advanceDays(5);
    require(sim.state().date.day == 5, "advancing 5 days reaches day 5");
    require(events.empty(), "pure mining days emit no audit events");
    require(sim.state().eventLog.empty(), "pure mining days do not append audit history");
    require(sim.state().dailyEconomySnapshots.size() == 10,
            "five days of two deposits creates ten telemetry rows");
    require(sim.state().dailyEconomySnapshots.front().day == 1, "telemetry captures first simulated day");
    require(sim.state().dailyEconomySnapshots.back().day == 5, "telemetry captures latest simulated day");
}

void test_mining() {
    // Verifies the basic colony-mines-to-stockpile loop. This protects the core
    // economic foundation for future industry and forecast systems.
    deep::Simulation sim{deep::createHomeSystemScenario()};

    const double startingStructural = sim.state().colonies.front().stockpile.get(deep::Mineral::Structural);
    const double startingDeposit = sim.state().mineralDeposits.front().remaining;

    sim.advanceDays(1);

    const double endingStructural = sim.state().colonies.front().stockpile.get(deep::Mineral::Structural);
    const double endingDeposit = sim.state().mineralDeposits.front().remaining;

    require(endingStructural > startingStructural, "mining increases structural stockpile");
    require(endingDeposit < startingDeposit, "mining decreases deposit");
    require(sim.state().dailyEconomySnapshots.size() == 2, "one mining day creates telemetry for both deposits");

    const deep::DailyEconomySnapshot& structuralTelemetry = sim.state().dailyEconomySnapshots.front();
    require(structuralTelemetry.day == 1, "mining telemetry records the production day");
    require(structuralTelemetry.colonyId == sim.state().colonies.front().id, "mining telemetry records colony ID");
    require(structuralTelemetry.bodyId == sim.state().colonies.front().bodyId, "mining telemetry records body ID");
    require(structuralTelemetry.mineral == deep::Mineral::Structural, "mining telemetry records mineral type");
    require(structuralTelemetry.amount > 0.0, "mining telemetry records extracted amount");
    require(structuralTelemetry.remainingDeposit == endingDeposit, "mining telemetry records remaining deposit");
}

void test_shipyard_completion() {
    // Verifies that a command-created shipyard order consumes build time and
    // produces a ship plus fleet. Prevents direct-state-mutation regressions.
    deep::Simulation sim{deep::createHomeSystemScenario()};

    const deep::ColonyId colonyId = sim.state().colonies.front().id;
    const deep::ShipClassId shipClassId = sim.state().shipClasses.front().id;

    const auto result = sim.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    });

    require(result.ok, "shipyard build order is accepted");

    sim.advanceDays(5);

    require(sim.state().ships.size() == 1, "shipyard creates one ship after 5 days");
    require(sim.state().fleets.size() == 1, "ship completion creates one fleet");
    require(sim.state().shipyardOrders.front().status == deep::ShipyardOrderStatus::Completed,
            "shipyard order is completed");
    require(sim.state().eventLog.size() == 2,
            "shipyard order creation and completion remain player-facing audit events");
    require(std::holds_alternative<deep::ShipCompletedEvent>(sim.state().eventLog.back().payload),
            "ship completion remains in event log");
}

void test_shipyard_capacity_is_shared_by_fifo_orders() {
    // Regression test for colony-level capacity allocation. Multiple active
    // orders at the same colony must not each receive a full daily capacity
    // grant; the first order consumes the pool before later orders can progress.
    deep::Simulation sim{deep::createHomeSystemScenario()};

    const deep::ColonyId colonyId = sim.state().colonies.front().id;
    const deep::ShipClassId shipClassId = sim.state().shipClasses.front().id;

    require(sim.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "first build order is accepted");
    require(sim.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "second build order is accepted");

    sim.advanceDays(5);

    require(sim.state().shipyardOrders.size() == 2, "two shipyard orders remain tracked");
    require(sim.state().ships.size() == 1, "five days of 100 BP/day completes only one 500 BP order");
    require(sim.state().shipyardOrders.at(0).status == deep::ShipyardOrderStatus::Completed,
            "FIFO order receives colony capacity first");
    require(sim.state().shipyardOrders.at(1).status == deep::ShipyardOrderStatus::Active,
            "second FIFO order waits for later daily capacity");
    require(sim.state().shipyardOrders.at(1).quantityCompleted == 0,
            "second FIFO order does not complete from duplicated capacity");
    require(sim.state().shipyardOrders.at(1).accumulatedBuildPoints == 0.0,
            "second FIFO order receives no capacity while the first order is consuming the pool");

    sim.advanceDays(5);

    require(sim.state().ships.size() == 2, "second order completes after receiving the next five days of capacity");
    require(sim.state().shipyardOrders.at(1).status == deep::ShipyardOrderStatus::Completed,
            "second FIFO order eventually completes after the first order finishes");
}


void test_shipyard_temporary_mineral_shortage_recovers() {
    // Verifies that mineral shortages pause production without permanently
    // blocking the order. This prevents a deadlock where future mining produces
    // enough minerals but the order is skipped forever because its status changed.
    deep::GameState state = deep::createHomeSystemScenario();
    state.colonies.front().stockpile.set(deep::Mineral::Structural, 0.0);
    state.colonies.front().stockpile.set(deep::Mineral::Propulsion, 1'000.0);
    state.colonies.front().stockpile.set(deep::Mineral::Electronics, 1'000.0);

    deep::Simulation sim{std::move(state)};
    const deep::ColonyId colonyId = sim.state().colonies.front().id;
    const deep::ShipClassId shipClassId = sim.state().shipClasses.front().id;

    require(sim.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order accepted before shortage test");

    sim.advanceDays(5);
    require(sim.state().ships.empty(), "ship does not complete before minerals are affordable");
    require(sim.state().shipyardOrders.front().status == deep::ShipyardOrderStatus::Active,
            "temporary mineral shortage leaves order active");

    sim.advanceDays(45);
    require(sim.state().ships.size() == 1, "ship completes after mining supplies missing minerals");
    require(sim.state().shipyardOrders.front().status == deep::ShipyardOrderStatus::Completed,
            "recovered order completes instead of remaining paused");
}

void test_fleet_movement() {
    // Verifies the movement command lifecycle: accepted order, active movement,
    // arrival, and cleanup. Prevents stale destination/order state after arrival.
    deep::Simulation sim{deep::createHomeSystemScenario()};

    const deep::ColonyId colonyId = sim.state().colonies.front().id;
    const deep::ShipClassId shipClassId = sim.state().shipClasses.front().id;
    const deep::BodyId marsId = sim.state().bodies.at(1).id;

    require(sim.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order accepted before movement test");

    sim.advanceDays(5);

    const deep::FleetId fleetId = sim.state().fleets.front().id;
    require(sim.execute(deep::MoveFleetCommand{
        .fleetId = fleetId,
        .destinationBodyId = marsId
    }).ok, "move order accepted");

    require(sim.state().fleets.front().activeOrder.type == deep::FleetOrderType::MoveToBody,
            "fleet has active move order");

    sim.advanceDays(5);

    require(sim.state().fleets.front().currentBodyId == marsId, "fleet arrives at Mars after fixed duration");
    require(sim.state().fleets.front().activeOrder.type == deep::FleetOrderType::None,
            "fleet clears active order after arrival");
    require(std::holds_alternative<deep::FleetArrivedEvent>(sim.state().eventLog.back().payload),
            "fleet arrival remains in event log");
}

void test_cancel_fleet_order() {
    // Verifies that the player can cancel an active movement order without
    // teleporting the fleet. This protects the first UI cancel button from
    // leaving stale destination or active-order state behind.
    deep::Simulation sim{deep::createHomeSystemScenario()};

    const deep::ColonyId colonyId = sim.state().colonies.front().id;
    const deep::ShipClassId shipClassId = sim.state().shipClasses.front().id;
    const deep::BodyId terraId = sim.state().bodies.front().id;
    const deep::BodyId marsId = sim.state().bodies.at(1).id;

    require(sim.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order accepted before cancel test");
    sim.advanceDays(5);

    const deep::FleetId fleetId = sim.state().fleets.front().id;
    require(sim.execute(deep::MoveFleetCommand{
        .fleetId = fleetId,
        .destinationBodyId = marsId
    }).ok, "move order accepted before cancel test");

    sim.advanceDays(2);
    require(sim.state().fleets.front().activeOrder.daysRemaining == 3,
            "movement countdown advances before cancellation");

    const auto cancelResult = sim.execute(deep::CancelFleetOrderCommand{
        .fleetId = fleetId
    });

    require(cancelResult.ok, "active fleet order can be cancelled");
    require(sim.state().fleets.front().currentBodyId == terraId,
            "cancelled fleet remains at its current body");
    require(!sim.state().fleets.front().destinationBodyId.has_value(),
            "cancelled fleet clears destination body");
    require(sim.state().fleets.front().activeOrder.type == deep::FleetOrderType::None,
            "cancelled fleet clears active order type");
    require(sim.state().fleets.front().activeOrder.daysRemaining == 0,
            "cancelled fleet clears remaining order time");

    sim.advanceDays(5);
    require(sim.state().fleets.front().currentBodyId == terraId,
            "cancelled fleet does not arrive after the old duration elapses");
}

void test_rejected_invalid_command() {
    // Verifies that invalid commands fail through CommandResult and are recorded
    // in the audit log. Prevents silent validation failures in future UI code.
    deep::Simulation sim{deep::createHomeSystemScenario()};

    const auto result = sim.execute(deep::AssignShipyardBuildCommand{
        .colonyId = deep::ColonyId{999},
        .shipClassId = sim.state().shipClasses.front().id,
        .quantity = 1
    });

    require(!result.ok, "invalid colony is rejected");
    require(!sim.state().eventLog.empty(), "rejection is written to event log");
}

} // namespace

// Executes all regression tests. Additional tests can be added here until the
// project adopts Catch2 or another approved open-source test framework.
int main() {
    try {
        test_time_advancement();
        test_mining();
        test_shipyard_completion();
        test_shipyard_capacity_is_shared_by_fifo_orders();
        test_shipyard_temporary_mineral_shortage_recovers();
        test_fleet_movement();
        test_cancel_fleet_order();
        test_rejected_invalid_command();
    } catch (const std::exception& ex) {
        std::cerr << "Test failure: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "All Deep Signal simulation tests passed.\n";
    return EXIT_SUCCESS;
}
