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


void test_mineral_subtraction_clamps_epsilon_negative_residue() {
    // Verifies that affordability and subtraction use the same tolerance.
    // Without the clamp, canPay() can allow an epsilon-sized shortage and
    // subtract() can leave a tiny negative physical stockpile behind.
    deep::MineralSet stockpile;
    deep::MineralSet cost;

    stockpile.set(deep::Mineral::Iron, 500.0);
    cost.set(deep::Mineral::Iron, 500.0 + (deep::kMineralComparisonEpsilon * 0.5));

    require(stockpile.canPay(cost), "epsilon-sized mineral residue is payable");

    stockpile.subtract(cost);

    require(stockpile.get(deep::Mineral::Iron) == 0.0,
            "epsilon-sized negative mineral residue clamps exactly to zero");
}

void test_mineral_can_pay_rejects_meaningful_shortage() {
    // Verifies that the epsilon is only a floating-point tolerance and does not
    // mask real affordability failures in production or maintenance costs.
    deep::MineralSet stockpile;
    deep::MineralSet cost;

    stockpile.set(deep::Mineral::Iron, 500.0);
    cost.set(deep::Mineral::Iron, 501.0);

    require(!stockpile.canPay(cost), "meaningful mineral shortage is not payable");

    bool threw = false;
    try {
        stockpile.subtract(cost);
    } catch (const std::runtime_error&) {
        threw = true;
    }

    require(threw, "meaningful mineral shortage fails explicitly on subtract");
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
    require(sim.state().dailyEconomySnapshots.size() == 35,
            "five days of seven Terra deposits creates thirty-five telemetry rows");
    require(sim.state().dailyEconomySnapshots.front().day == 1, "telemetry captures first simulated day");
    require(sim.state().dailyEconomySnapshots.back().day == 5, "telemetry captures latest simulated day");
}

void test_mining() {
    // Verifies the basic colony-mines-to-stockpile loop. Processor capacity is
    // disabled here so the test isolates extraction from downstream conversion.
    deep::GameState state = deep::createHomeSystemScenario();
    state.colonies.front().processorCapacity = 0.0;
    deep::Simulation sim{std::move(state)};

    const double startingIron = sim.state().colonies.front().stockpile.get(deep::Mineral::Iron);
    const double startingDeposit = sim.state().mineralDeposits.front().remaining;

    sim.advanceDays(1);

    const double endingIron = sim.state().colonies.front().stockpile.get(deep::Mineral::Iron);
    const double endingDeposit = sim.state().mineralDeposits.front().remaining;

    require(endingIron > startingIron, "mining increases iron stockpile when processors are disabled");
    require(endingDeposit < startingDeposit, "mining decreases deposit");
    require(sim.state().dailyEconomySnapshots.size() == 7, "one mining day creates telemetry for all Terra deposits");

    const deep::DailyEconomySnapshot& ironTelemetry = sim.state().dailyEconomySnapshots.front();
    require(ironTelemetry.day == 1, "mining telemetry records the production day");
    require(ironTelemetry.colonyId == sim.state().colonies.front().id, "mining telemetry records colony ID");
    require(ironTelemetry.bodyId == sim.state().colonies.front().bodyId, "mining telemetry records body ID");
    require(ironTelemetry.mineral == deep::Mineral::Iron, "mining telemetry records mineral type");
    require(ironTelemetry.amount > 0.0, "mining telemetry records extracted amount");
    require(ironTelemetry.remainingDeposit == endingDeposit, "mining telemetry records remaining deposit");
}

void test_processing_converts_raw_minerals_to_processed_materials() {
    // Verifies the first raw-resource to processed-material link. Shipyard costs
    // rely on these processed stockpiles rather than consuming raw minerals.
    deep::GameState state = deep::createHomeSystemScenario();
    state.colonies.front().mines = 0.0;
    deep::Simulation sim{std::move(state)};

    const double startingIron = sim.state().colonies.front().stockpile.get(deep::Mineral::Iron);
    const double startingAlloys = sim.state().colonies.front().processedStockpile.get(deep::ProcessedMaterial::StructuralAlloys);

    sim.advanceDays(1);

    const double endingIron = sim.state().colonies.front().stockpile.get(deep::Mineral::Iron);
    const double endingAlloys = sim.state().colonies.front().processedStockpile.get(deep::ProcessedMaterial::StructuralAlloys);

    require(endingAlloys > startingAlloys, "daily processors create structural alloys");
    require(endingIron < startingIron, "processing consumes more iron than mining adds in the starter scenario");
}


void test_manual_processing_policy_directs_processor_capacity() {
    // Verifies that player-selected Manual policy changes actual daily
    // processing output. This prevents the allocation UI from becoming a
    // display-only control disconnected from simulation rules.
    deep::Simulation sim{deep::createHomeSystemScenario()};
    const deep::ColonyId colonyId = sim.state().colonies.front().id;

    const double startingAlloys = sim.state().colonies.front().processedStockpile.get(deep::ProcessedMaterial::StructuralAlloys);
    const double startingElectronics = sim.state().colonies.front().processedStockpile.get(deep::ProcessedMaterial::Electronics);

    const auto policyResult = sim.execute(deep::SetColonyProcessingPolicyCommand{
        .colonyId = colonyId,
        .policy = deep::ProcessingPolicy::Manual,
        .manualAllocations = {
            deep::ProcessingAllocation{.material = deep::ProcessedMaterial::Electronics, .weight = 100.0}
        }
    });

    require(policyResult.ok, "manual processing policy command is accepted");

    sim.advanceDays(1);

    require(sim.state().colonies.front().processingPolicy == deep::ProcessingPolicy::Manual,
            "manual processing policy is stored on the colony");
    require(sim.state().colonies.front().processedStockpile.get(deep::ProcessedMaterial::Electronics) > startingElectronics,
            "manual policy allocates processor output to electronics");
    require(sim.state().colonies.front().processedStockpile.get(deep::ProcessedMaterial::StructuralAlloys) == startingAlloys,
            "manual policy does not also allocate capacity to structural alloys");
}

void test_manual_processing_policy_requires_positive_weight() {
    // Verifies command validation for Manual policy. A manual policy without any
    // positive weights would otherwise silently spend no processor capacity.
    deep::Simulation sim{deep::createHomeSystemScenario()};
    const deep::ColonyId colonyId = sim.state().colonies.front().id;

    const auto result = sim.execute(deep::SetColonyProcessingPolicyCommand{
        .colonyId = colonyId,
        .policy = deep::ProcessingPolicy::Manual,
        .manualAllocations = {}
    });

    require(!result.ok, "manual processing policy with no positive weights is rejected");
    require(sim.state().colonies.front().processingPolicy == deep::ProcessingPolicy::Balanced,
            "rejected manual policy leaves the existing processing policy unchanged");
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


void test_shipyard_temporary_processed_material_shortage_recovers() {
    // Verifies that processed-material shortages pause production without
    // permanently blocking the order. Processors keep running, so the order can
    // complete once enough materials accumulate.
    deep::GameState state = deep::createHomeSystemScenario();
    state.colonies.front().processedStockpile.set(deep::ProcessedMaterial::StructuralAlloys, 0.0);
    state.colonies.front().processorCapacity = 20.0;
    state.colonies.front().processingPolicy = deep::ProcessingPolicy::Manual;
    state.colonies.front().manualProcessingAllocations = {
        deep::ProcessingAllocation{.material = deep::ProcessedMaterial::StructuralAlloys, .weight = 100.0}
    };

    deep::Simulation sim{std::move(state)};
    const deep::ColonyId colonyId = sim.state().colonies.front().id;
    const deep::ShipClassId shipClassId = sim.state().shipClasses.front().id;

    require(sim.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order accepted before shortage test");

    sim.advanceDays(5);
    require(sim.state().ships.empty(), "ship does not complete before processed materials are affordable");
    require(sim.state().shipyardOrders.front().status == deep::ShipyardOrderStatus::Active,
            "temporary processed-material shortage leaves order active");

    sim.advanceDays(20);
    require(sim.state().ships.size() == 1, "ship completes after processors supply missing materials");
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
        test_mineral_subtraction_clamps_epsilon_negative_residue();
        test_mineral_can_pay_rejects_meaningful_shortage();
        test_time_advancement();
        test_mining();
        test_processing_converts_raw_minerals_to_processed_materials();
        test_manual_processing_policy_directs_processor_capacity();
        test_manual_processing_policy_requires_positive_weight();
        test_shipyard_completion();
        test_shipyard_capacity_is_shared_by_fifo_orders();
        test_shipyard_temporary_processed_material_shortage_recovers();
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
