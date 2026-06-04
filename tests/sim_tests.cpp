#include "sim/Commands.h"
#include "sim/Events.h"
#include "sim/Minerals.h"
#include "sim/ScenarioFactory.h"
#include "sim/Simulation.h"

// Self-contained regression tests for the headless simulation layer.
// These tests avoid third-party dependencies for Phase 1, while still exercising
// the command API, daily tick order, economy, production, movement, and event log.

#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
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

void requireNear(const double actual, const double expected, const std::string_view message) {
    if (std::abs(actual - expected) > 1.0e-6) {
        throw TestFailure{message};
    }
}

deep::PersonId personIdByName(const deep::GameState& state, const std::string_view name) {
    for (const deep::Person& person : state.people) {
        if (person.name == name) {
            return person.id;
        }
    }
    throw TestFailure{"expected person was not present in scenario"};
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
    require(sim.state().dailyEconomySnapshots.size() == 95,
            "five days across mature-system mining colonies creates ninety-five telemetry rows");
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
    require(sim.state().dailyEconomySnapshots.size() == 19, "one mining day creates telemetry for all active mature-system deposits");

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

void test_manual_processing_weights_are_normalized() {
    // Verifies Manual allocation values are relative weights, not percentages.
    // A user can enter weights summing above or below 100 and the simulation
    // still spends exactly one normalized processor-capacity pool per day.
    deep::GameState state = deep::createHomeSystemScenario();
    state.colonies.front().mines = 0.0;
    state.colonies.front().processorCapacity = 40.0;
    state.colonies.front().processedStockpile = deep::ProcessedMaterialSet{};
    deep::Simulation sim{std::move(state)};
    const deep::ColonyId colonyId = sim.state().colonies.front().id;

    const auto policyResult = sim.execute(deep::SetColonyProcessingPolicyCommand{
        .colonyId = colonyId,
        .policy = deep::ProcessingPolicy::Manual,
        .manualAllocations = {
            deep::ProcessingAllocation{.material = deep::ProcessedMaterial::StructuralAlloys, .weight = 3.0},
            deep::ProcessingAllocation{.material = deep::ProcessedMaterial::Electronics, .weight = 1.0}
        }
    });

    require(policyResult.ok, "manual processing weights are accepted");
    sim.advanceDays(1);

    const deep::Colony& colony = sim.state().colonies.front();
    requireNear(colony.processedStockpile.get(deep::ProcessedMaterial::StructuralAlloys),
                30.0,
                "manual weight 3 receives 75 percent of processor capacity");
    requireNear(colony.processedStockpile.get(deep::ProcessedMaterial::Electronics),
                10.0,
                "manual weight 1 receives 25 percent of processor capacity");
}

void test_non_manual_policy_preserves_manual_weights() {
    // Verifies preset policies do not overwrite the player's last Manual setup.
    // The UI can switch to a read-only preset preview and then back to Manual
    // without losing the stored manual weights.
    deep::Simulation sim{deep::createHomeSystemScenario()};
    const deep::ColonyId colonyId = sim.state().colonies.front().id;

    require(sim.execute(deep::SetColonyProcessingPolicyCommand{
        .colonyId = colonyId,
        .policy = deep::ProcessingPolicy::Manual,
        .manualAllocations = {
            deep::ProcessingAllocation{.material = deep::ProcessedMaterial::Propellant, .weight = 2.0}
        }
    }).ok, "initial manual processing policy is accepted");

    require(sim.execute(deep::SetColonyProcessingPolicyCommand{
        .colonyId = colonyId,
        .policy = deep::ProcessingPolicy::FuelFocus,
        .manualAllocations = {
            deep::ProcessingAllocation{.material = deep::ProcessedMaterial::Electronics, .weight = 9.0}
        }
    }).ok, "preset processing policy is accepted");

    const deep::Colony& colony = sim.state().colonies.front();
    require(colony.processingPolicy == deep::ProcessingPolicy::FuelFocus, "preset policy is stored");
    require(colony.manualProcessingAllocations.size() == 1, "preset policy does not replace manual weights");
    require(colony.manualProcessingAllocations.front().material == deep::ProcessedMaterial::Propellant,
            "last manual material is preserved while preset policy is active");
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

void test_manual_processing_policy_rejects_invalid_weights() {
    // Verifies command-level numeric validation before weights reach the daily
    // processing tick. NaN/Inf would otherwise normalize into non-finite shares
    // and poison raw or processed stockpile arithmetic.
    deep::Simulation sim{deep::createHomeSystemScenario()};
    const deep::ColonyId colonyId = sim.state().colonies.front().id;

    const auto negativeResult = sim.execute(deep::SetColonyProcessingPolicyCommand{
        .colonyId = colonyId,
        .policy = deep::ProcessingPolicy::Manual,
        .manualAllocations = {
            deep::ProcessingAllocation{.material = deep::ProcessedMaterial::Electronics, .weight = -1.0}
        }
    });
    require(!negativeResult.ok, "negative manual processing weights are rejected");

    const auto nanResult = sim.execute(deep::SetColonyProcessingPolicyCommand{
        .colonyId = colonyId,
        .policy = deep::ProcessingPolicy::Manual,
        .manualAllocations = {
            deep::ProcessingAllocation{.material = deep::ProcessedMaterial::Electronics,
                                       .weight = std::numeric_limits<double>::quiet_NaN()}
        }
    });
    require(!nanResult.ok, "NaN manual processing weights are rejected");

    const auto infiniteResult = sim.execute(deep::SetColonyProcessingPolicyCommand{
        .colonyId = colonyId,
        .policy = deep::ProcessingPolicy::Manual,
        .manualAllocations = {
            deep::ProcessingAllocation{.material = deep::ProcessedMaterial::Electronics,
                                       .weight = std::numeric_limits<double>::infinity()}
        }
    });
    require(!infiniteResult.ok, "infinite manual processing weights are rejected");
    require(sim.state().colonies.front().processingPolicy == deep::ProcessingPolicy::Balanced,
            "rejected invalid weights leave the existing processing policy unchanged");
}

void test_processing_policy_command_rejects_invalid_enum() {
    // Verifies command validation for the policy enum itself. UI/save boundaries
    // must not be able to store an ordinal that no policy switch handles.
    deep::Simulation sim{deep::createHomeSystemScenario()};
    const deep::ColonyId colonyId = sim.state().colonies.front().id;

    const auto result = sim.execute(deep::SetColonyProcessingPolicyCommand{
        .colonyId = colonyId,
        .policy = static_cast<deep::ProcessingPolicy>(999),
        .manualAllocations = {}
    });

    require(!result.ok, "invalid processing policy enum is rejected");
    require(sim.state().colonies.front().processingPolicy == deep::ProcessingPolicy::Balanced,
            "rejected invalid policy leaves the existing processing policy unchanged");
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
    require(sim.state().ships.size() == 1, "five days of appointment-modified BP/day completes only one 500 BP order");
    require(sim.state().shipyardOrders.at(0).status == deep::ShipyardOrderStatus::Completed,
            "FIFO order receives colony capacity first");
    require(sim.state().shipyardOrders.at(1).status == deep::ShipyardOrderStatus::Active,
            "second FIFO order waits for later daily capacity");
    require(sim.state().shipyardOrders.at(1).quantityCompleted == 0,
            "second FIFO order does not complete from duplicated capacity");
    requireNear(sim.state().shipyardOrders.at(1).accumulatedBuildPoints, 50.0,
                "second FIFO order receives only leftover modified capacity after the first order completes");

    sim.advanceDays(5);

    require(sim.state().ships.size() == 2, "second order completes after receiving the next days of modified capacity");
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
    const double startingFuel = sim.state().ships.front().fuel;
    require(sim.execute(deep::MoveFleetCommand{
        .fleetId = fleetId,
        .destinationBodyId = marsId
    }).ok, "move order accepted");

    require(sim.state().fleets.front().activeOrder.type == deep::FleetOrderType::MoveToBody,
            "fleet has active move order");
    requireNear(sim.state().ships.front().fuel,
                startingFuel - 240.0,
                "starting a Terra-to-Mars move consumes map-distance fuel immediately");

    sim.advanceDays(5);

    require(sim.state().fleets.front().currentBodyId == marsId, "fleet arrives at Mars after fixed duration");
    require(sim.state().fleets.front().activeOrder.type == deep::FleetOrderType::None,
            "fleet clears active order after arrival");
    require(std::holds_alternative<deep::FleetArrivedEvent>(sim.state().eventLog.back().payload),
            "fleet arrival remains in event log");
}


void test_fleet_movement_rejects_insufficient_fuel() {
    // Verifies movement is now an operational fuel decision. A fleet with no
    // propellant cannot start a move, and the rejected command leaves order and
    // location state untouched.
    deep::Simulation setup{deep::createHomeSystemScenario()};

    const deep::ColonyId colonyId = setup.state().colonies.front().id;
    const deep::ShipClassId shipClassId = setup.state().shipClasses.front().id;
    const deep::BodyId terraId = setup.state().bodies.front().id;
    const deep::BodyId marsId = setup.state().bodies.at(1).id;

    require(setup.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order accepted before no-fuel movement test");
    setup.advanceDays(5);

    deep::GameState state = setup.state();
    state.ships.front().fuel = 0.0;
    deep::Simulation sim{std::move(state)};

    const deep::FleetId fleetId = sim.state().fleets.front().id;
    const auto result = sim.execute(deep::MoveFleetCommand{
        .fleetId = fleetId,
        .destinationBodyId = marsId
    });

    require(!result.ok, "no-fuel fleet move is rejected");
    require(sim.state().fleets.front().currentBodyId == terraId,
            "rejected no-fuel move leaves fleet at origin");
    require(sim.state().fleets.front().activeOrder.type == deep::FleetOrderType::None,
            "rejected no-fuel move does not create an active order");
    requireNear(sim.state().ships.front().fuel, 0.0, "rejected no-fuel move does not consume negative fuel");
}


void test_fleet_commander_reduces_move_fuel_cost_within_cap() {
    // Verifies appointment effects change a real operation, but only within the
    // tight v1 cap. A strong fleet commander reduces Terra-Mars fuel cost by 10%.
    deep::Simulation sim{deep::createHomeSystemScenario()};

    const deep::ColonyId colonyId = sim.state().colonies.front().id;
    const deep::ShipClassId shipClassId = sim.state().shipClasses.front().id;
    const deep::BodyId marsId = sim.state().bodies.at(1).id;
    const deep::PersonId commanderId = personIdByName(sim.state(), "Commodore Elias Voss");

    require(sim.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order accepted before fleet commander fuel test");
    sim.advanceDays(5);

    const deep::FleetId fleetId = sim.state().fleets.front().id;
    require(sim.execute(deep::AssignAppointmentCommand{
        .role = deep::AppointmentRole::FleetCommander,
        .scopeType = deep::AppointmentScopeType::Fleet,
        .scopeId = fleetId.value,
        .personId = commanderId
    }).ok, "fleet commander appointment is accepted before movement");

    require(sim.execute(deep::MoveFleetCommand{
        .fleetId = fleetId,
        .destinationBodyId = marsId
    }).ok, "fleet move with appointed commander is accepted");

    requireNear(sim.state().ships.front().fuel,
                784.0,
                "fleet commander capped modifier reduces 240 fuel cost to 216");
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

void test_fleet_order_queue_starts_next_order_after_arrival() {
    // Verifies the v1 queue lifecycle: the first queued move starts immediately
    // for an idle fleet, future orders remain visible, and arrival promotes the
    // next queued order with a fleet-order-assigned event.
    deep::Simulation sim{deep::createHomeSystemScenario()};

    const deep::ColonyId colonyId = sim.state().colonies.front().id;
    const deep::ShipClassId shipClassId = sim.state().shipClasses.front().id;
    const deep::BodyId terraId = sim.state().bodies.front().id;
    const deep::BodyId marsId = sim.state().bodies.at(1).id;

    require(sim.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order accepted before queue test");
    sim.advanceDays(5);

    const deep::FleetId fleetId = sim.state().fleets.front().id;
    require(sim.execute(deep::QueueFleetMoveOrderCommand{
        .fleetId = fleetId,
        .destinationBodyId = marsId
    }).ok, "idle fleet starts first queued move immediately");
    require(sim.state().fleets.front().activeOrder.type == deep::FleetOrderType::MoveToBody,
            "first queued move becomes current order");
    require(sim.state().fleets.front().queuedOrders.empty(),
            "started queued order is removed from queue");

    require(sim.execute(deep::QueueFleetMoveOrderCommand{
        .fleetId = fleetId,
        .destinationBodyId = terraId
    }).ok, "active fleet accepts a follow-up queued move");
    require(sim.state().fleets.front().queuedOrders.size() == 1,
            "follow-up move remains queued while current order is active");

    const std::size_t eventCountBeforeArrival = sim.state().eventLog.size();
    sim.advanceDays(5);

    require(sim.state().fleets.front().currentBodyId == marsId,
            "fleet reaches the first queued destination");
    require(sim.state().fleets.front().activeOrder.type == deep::FleetOrderType::MoveToBody,
            "follow-up queued order starts after first order completes");
    require(sim.state().fleets.front().activeOrder.targetBodyId == terraId,
            "follow-up queued order targets Terra");
    require(sim.state().fleets.front().queuedOrders.empty(),
            "promoted queued order is removed from queue");
    require(sim.state().eventLog.size() == eventCountBeforeArrival + 2,
            "arrival and queued-order-start events are both logged");
    require(std::holds_alternative<deep::FleetOrderAssignedEvent>(sim.state().eventLog.back().payload),
            "queued order start is recorded as a fleet-order assignment event");

    sim.advanceDays(5);
    require(sim.state().fleets.front().currentBodyId == terraId,
            "fleet completes the follow-up queued move");
    require(sim.state().fleets.front().activeOrder.type == deep::FleetOrderType::None,
            "fleet is idle after queued moves are exhausted");
}

void test_clear_fleet_order_queue_preserves_current_order() {
    // Verifies that clearing queued intent does not cancel the current order.
    // This keeps the Fleet Orders panel's Clear Queue button separate from the
    // Cancel Order button.
    deep::Simulation sim{deep::createHomeSystemScenario()};

    const deep::ColonyId colonyId = sim.state().colonies.front().id;
    const deep::ShipClassId shipClassId = sim.state().shipClasses.front().id;
    const deep::BodyId terraId = sim.state().bodies.front().id;
    const deep::BodyId marsId = sim.state().bodies.at(1).id;

    require(sim.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order accepted before clear-queue test");
    sim.advanceDays(5);

    const deep::FleetId fleetId = sim.state().fleets.front().id;
    require(sim.execute(deep::QueueFleetMoveOrderCommand{
        .fleetId = fleetId,
        .destinationBodyId = marsId
    }).ok, "first queued move starts before clear-queue test");
    require(sim.execute(deep::QueueFleetMoveOrderCommand{
        .fleetId = fleetId,
        .destinationBodyId = terraId
    }).ok, "follow-up move queues before clear-queue test");

    const auto clearResult = sim.execute(deep::ClearFleetOrderQueueCommand{.fleetId = fleetId});
    require(clearResult.ok, "fleet queue can be cleared");
    require(sim.state().fleets.front().queuedOrders.empty(), "future queued orders are cleared");
    require(sim.state().fleets.front().activeOrder.type == deep::FleetOrderType::MoveToBody,
            "current order survives queue clearing");

    sim.advanceDays(5);
    require(sim.state().fleets.front().currentBodyId == marsId,
            "current order still completes after queue is cleared");
    require(sim.state().fleets.front().activeOrder.type == deep::FleetOrderType::None,
            "no follow-up order starts after queue is cleared");
}

void test_assign_appointment_command_replaces_current_slot() {
    // Verifies appointments are mutated through the command boundary and remain
    // one current record per role/scope slot. Reassignment updates responsibility
    // without adding gameplay modifiers or duplicate historical rows yet.
    deep::Simulation sim{deep::createHomeSystemScenario()};
    sim.advanceDays(2);

    const deep::ColonyId colonyId = sim.state().colonies.front().id;
    const deep::PersonId replacementPersonId = sim.state().people.at(2).id;

    const auto result = sim.execute(deep::AssignAppointmentCommand{
        .role = deep::AppointmentRole::ShipyardDirector,
        .scopeType = deep::AppointmentScopeType::Colony,
        .scopeId = colonyId.value,
        .personId = replacementPersonId
    });

    require(result.ok, "valid appointment assignment is accepted");

    int matchingSlots = 0;
    const deep::Appointment* matchedAppointment = nullptr;
    for (const deep::Appointment& appointment : sim.state().appointments) {
        if (appointment.role == deep::AppointmentRole::ShipyardDirector &&
            appointment.scopeType == deep::AppointmentScopeType::Colony &&
            appointment.scopeId == colonyId.value) {
            ++matchingSlots;
            matchedAppointment = &appointment;
        }
    }

    require(matchingSlots == 1, "appointment reassignment preserves one row per slot");
    require(matchedAppointment != nullptr && matchedAppointment->personId == replacementPersonId,
            "appointment records the replacement person");
    require(matchedAppointment != nullptr && matchedAppointment->appointedDay == sim.state().date.day,
            "appointment records the reassignment day");
}

void test_assign_appointment_command_rejects_invalid_target() {
    // Verifies command-time validation rejects dangling personnel and scope IDs
    // before they can enter GameState and later be persisted.
    deep::Simulation sim{deep::createHomeSystemScenario()};

    const auto missingPerson = sim.execute(deep::AssignAppointmentCommand{
        .role = deep::AppointmentRole::InstitutionHead,
        .scopeType = deep::AppointmentScopeType::Institution,
        .scopeId = sim.state().institutions.front().id.value,
        .personId = deep::PersonId{999}
    });
    require(!missingPerson.ok, "appointment command rejects missing person");

    const auto missingScope = sim.execute(deep::AssignAppointmentCommand{
        .role = deep::AppointmentRole::InstitutionHead,
        .scopeType = deep::AppointmentScopeType::Institution,
        .scopeId = 999,
        .personId = sim.state().people.front().id
    });
    require(!missingScope.ok, "appointment command rejects missing scope");
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
        test_manual_processing_weights_are_normalized();
        test_non_manual_policy_preserves_manual_weights();
        test_manual_processing_policy_requires_positive_weight();
        test_manual_processing_policy_rejects_invalid_weights();
        test_processing_policy_command_rejects_invalid_enum();
        test_shipyard_completion();
        test_shipyard_capacity_is_shared_by_fifo_orders();
        test_shipyard_temporary_processed_material_shortage_recovers();
        test_fleet_movement();
        test_fleet_movement_rejects_insufficient_fuel();
        test_fleet_commander_reduces_move_fuel_cost_within_cap();
        test_cancel_fleet_order();
        test_fleet_order_queue_starts_next_order_after_arrival();
        test_clear_fleet_order_queue_preserves_current_order();
        test_assign_appointment_command_replaces_current_slot();
        test_assign_appointment_command_rejects_invalid_target();
        test_rejected_invalid_command();
    } catch (const std::exception& ex) {
        std::cerr << "Test failure: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "All Deep Signal simulation tests passed.\n";
    return EXIT_SUCCESS;
}
