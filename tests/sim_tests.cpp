#include "sim/Commands.h"
#include "sim/Events.h"
#include "sim/Minerals.h"
#include "sim/ScenarioFactory.h"
#include "sim/ShipDesignRules.h"
#include "sim/Simulation.h"

// Self-contained regression tests for the headless simulation layer.
// These tests avoid third-party dependencies for Phase 1, while still exercising
// the command API, daily tick order, economy, production, movement, and event log.

#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

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
    if (!std::isfinite(actual) || !std::isfinite(expected) ||
        std::abs(actual - expected) > 1.0e-6) {
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

deep::BodyId bodyIdByName(const deep::GameState& state, const std::string_view name) {
    for (const deep::Body& body : state.bodies) {
        if (body.name == name) {
            return body.id;
        }
    }
    throw TestFailure{"expected body was not present in scenario"};
}

deep::FleetId addTestFleetAt(deep::GameState& state, const deep::BodyId bodyId) {
    // Tests that target command validation need a fleet at non-colony bodies.
    // Create the minimal bidirectional ship/fleet records that GameState
    // validation requires, using the scenario's first survey-capable class.
    const deep::FleetId fleetId{state.ids.nextFleetId++};
    const deep::ShipId shipId{state.ids.nextShipId++};
    const deep::ShipClass& shipClass = state.shipClasses.front();

    state.fleets.push_back(deep::Fleet{
        .id = fleetId,
        .name = "Test Survey Fleet",
        .currentBodyId = bodyId,
        .destinationBodyId = std::nullopt,
        .shipIds = {shipId},
        .activeOrder = deep::FleetOrder{},
        .queuedOrders = {},
        .ownerInstitutionId = std::nullopt
    });
    state.ships.push_back(deep::Ship{
        .id = shipId,
        .shipClassId = shipClass.id,
        .name = "Test Survey Cutter",
        .fleetId = fleetId,
        .fuel = deep::evaluateShipDesign(state.shipComponents, shipClass.components).propellantCapacity
    });

    return fleetId;
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
            deep::ProcessingAllocation{.material = deep::ProcessedMaterial::Propellant, .weight = 2.0},
            deep::ProcessingAllocation{.material = deep::ProcessedMaterial::Electronics, .weight = 3.0},
            deep::ProcessingAllocation{.material = deep::ProcessedMaterial::Propellant, .weight = 4.0}
        }
    }).ok, "initial manual processing policy is accepted");

    const std::vector<deep::ProcessingAllocation> manualBefore =
        sim.state().colonies.front().manualProcessingAllocations;

    require(sim.execute(deep::SetColonyProcessingPolicyCommand{
        .colonyId = colonyId,
        .policy = deep::ProcessingPolicy::FuelFocus,
        .manualAllocations = {
            deep::ProcessingAllocation{.material = deep::ProcessedMaterial::Electronics, .weight = 9.0}
        }
    }).ok, "preset processing policy is accepted");

    const deep::Colony& colony = sim.state().colonies.front();
    require(colony.processingPolicy == deep::ProcessingPolicy::FuelFocus, "preset policy is stored");
    require(colony.manualProcessingAllocations.size() == manualBefore.size(),
            "preset policy does not replace manual rows");
    for (std::size_t i = 0; i < manualBefore.size(); ++i) {
        require(colony.manualProcessingAllocations[i].material == manualBefore[i].material &&
                colony.manualProcessingAllocations[i].weight == manualBefore[i].weight,
                "preset policy preserves duplicate manual rows in submitted order and value");
    }
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

void requireOverflowRejectionPreservesGameplay(
    const deep::ProcessingPolicy policy,
    const std::vector<deep::ProcessingAllocation>& rows,
    const std::string_view label) {
    deep::Simulation sim{deep::createHomeSystemScenario()};
    const deep::ColonyId colonyId = sim.state().colonies.front().id;

    require(sim.execute(deep::SetColonyProcessingPolicyCommand{
        .colonyId = colonyId,
        .policy = deep::ProcessingPolicy::Manual,
        .manualAllocations = {
            {deep::ProcessedMaterial::Propellant, 2.0},
            {deep::ProcessedMaterial::Electronics, 3.0}
        }
    }).ok, "setup stores a nonempty prior Manual configuration");

    // Keep one prior rejection in the audit log so the test also verifies that
    // a later rejection appends once without rewriting earlier history.
    require(!sim.execute(deep::SetColonyProcessingPolicyCommand{
        .colonyId = deep::ColonyId{999999},
        .policy = deep::ProcessingPolicy::Balanced,
        .manualAllocations = {}
    }).ok, "setup rejection creates prior audit history");
    const deep::GameState before = sim.state();

    const auto result = sim.execute(deep::SetColonyProcessingPolicyCommand{
        .colonyId = colonyId, .policy = policy, .manualAllocations = rows
    });
    require(!result.ok, label);

    const deep::GameState& after = sim.state();
    const deep::Colony& priorColony = before.colonies.front();
    const deep::Colony& currentColony = after.colonies.front();
    require(after.date.day == before.date.day, "rejected processing command preserves date");
    require(currentColony.processingPolicy == priorColony.processingPolicy &&
            currentColony.manualProcessingAllocations.size() == priorColony.manualProcessingAllocations.size(),
            "rejected processing command preserves configuration");
    for (std::size_t i = 0; i < priorColony.manualProcessingAllocations.size(); ++i) {
        require(currentColony.manualProcessingAllocations[i].material ==
                    priorColony.manualProcessingAllocations[i].material &&
                currentColony.manualProcessingAllocations[i].weight ==
                    priorColony.manualProcessingAllocations[i].weight,
                "rejected processing command preserves prior Manual rows and order");
    }
    require(currentColony.stockpile.amount == priorColony.stockpile.amount &&
            currentColony.processedStockpile.amount == priorColony.processedStockpile.amount &&
            currentColony.mines == priorColony.mines &&
            currentColony.processorCapacity == priorColony.processorCapacity &&
            currentColony.shipyardCapacity == priorColony.shipyardCapacity,
            "rejected processing command preserves colony inventory and capacity");
    require(after.starSystems.size() == before.starSystems.size() &&
            after.bodies.size() == before.bodies.size() &&
            after.colonies.size() == before.colonies.size() &&
            after.shipyardOrders.size() == before.shipyardOrders.size() &&
            after.ships.size() == before.ships.size() &&
            after.fleets.size() == before.fleets.size(),
            "rejected processing command preserves entity and order counts");
    require(after.ids.nextStarSystemId == before.ids.nextStarSystemId &&
            after.ids.nextBodyId == before.ids.nextBodyId &&
            after.ids.nextColonyId == before.ids.nextColonyId &&
            after.ids.nextInstitutionId == before.ids.nextInstitutionId &&
            after.ids.nextPersonId == before.ids.nextPersonId &&
            after.ids.nextShipClassId == before.ids.nextShipClassId &&
            after.ids.nextShipyardOrderId == before.ids.nextShipyardOrderId &&
            after.ids.nextShipId == before.ids.nextShipId &&
            after.ids.nextFleetId == before.ids.nextFleetId,
            "rejected processing command preserves gameplay ID counters");
    require(after.ids.nextEventId == before.ids.nextEventId + 1 &&
            after.eventLog.size() == before.eventLog.size() + 1,
            "rejected processing command adds exactly one audit event and advances its counter once");
    require(after.eventLog.front().id == before.eventLog.front().id &&
            after.eventLog.front().day == before.eventLog.front().day &&
            std::get<deep::CommandRejectedEvent>(after.eventLog.front().payload).reason ==
                std::get<deep::CommandRejectedEvent>(before.eventLog.front().payload).reason,
            "rejected processing command preserves prior audit history");
    require(after.eventLog.back().id.value == before.ids.nextEventId &&
            after.eventLog.back().day == before.date.day &&
            std::holds_alternative<deep::CommandRejectedEvent>(after.eventLog.back().payload),
            "rejected processing command appends one current-day CommandRejectedEvent");
}

void test_processing_policy_rejects_aggregate_overflow_without_gameplay_mutation() {
    const double max = std::numeric_limits<double>::max();
    const std::vector<deep::ProcessingAllocation> sameMaterial{
        {deep::ProcessedMaterial::Electronics, max},
        {deep::ProcessedMaterial::Electronics, max}
    };
    const std::vector<deep::ProcessingAllocation> crossMaterial{
        {deep::ProcessedMaterial::StructuralAlloys, max},
        {deep::ProcessedMaterial::Electronics, max}
    };

    requireOverflowRejectionPreservesGameplay(deep::ProcessingPolicy::Manual, sameMaterial,
                                              "Manual command rejects duplicate-material overflow");
    requireOverflowRejectionPreservesGameplay(deep::ProcessingPolicy::Manual, crossMaterial,
                                              "Manual command rejects combined-total overflow");
    requireOverflowRejectionPreservesGameplay(deep::ProcessingPolicy::FuelFocus, sameMaterial,
                                              "preset command rejects invalid supplied manual rows");
}

void test_single_maximum_manual_weight_produces_finite_output() {
    deep::GameState state = deep::createHomeSystemScenario();
    state.colonies.front().mines = 0.0;
    state.colonies.front().processorCapacity = 40.0;
    state.colonies.front().processedStockpile = deep::ProcessedMaterialSet{};
    deep::Simulation sim{std::move(state)};
    const deep::ColonyId colonyId = sim.state().colonies.front().id;

    require(sim.execute(deep::SetColonyProcessingPolicyCommand{
        .colonyId = colonyId,
        .policy = deep::ProcessingPolicy::Manual,
        .manualAllocations = {{deep::ProcessedMaterial::Electronics,
                               std::numeric_limits<double>::max()}}
    }).ok, "one maximum finite manual weight is accepted");
    sim.advanceDays(1);

    const deep::Colony& colony = sim.state().colonies.front();
    requireNear(colony.processedStockpile.get(deep::ProcessedMaterial::Electronics), 40.0,
                "a single large Manual weight directs all capacity to Electronics");
    requireNear(colony.processedStockpile.get(deep::ProcessedMaterial::StructuralAlloys), 0.0,
                "a single large Manual weight does not allocate capacity elsewhere");
}

void test_stockpile_recovery_preserves_active_preset_cutoff() {
    // At 1e12 stored units, each Recovery weight is about 1e-12 and their
    // six-material total is below 1e-9. The pinned baseline allocated zero
    // processor capacity in this valid state, despite abundant raw minerals.
    const auto run = [](const double initialStockpile) {
        deep::GameState state = deep::createHomeSystemScenario();
        deep::Colony& ceres = state.colonies.at(2); // No shipyard capacity.
        ceres.mines = 0.0;
        ceres.processorCapacity = 60.0;
        ceres.stockpile.amount.fill(1'000'000.0);
        ceres.processedStockpile.amount.fill(initialStockpile);
        ceres.processingPolicy = deep::ProcessingPolicy::StockpileRecovery;
        deep::Simulation sim{std::move(state)};
        sim.advanceDays(1);
        for (const double amount : sim.state().colonies.at(2).processedStockpile.amount) {
            requireNear(amount - initialStockpile, initialStockpile == 0.0 ? 10.0 : 0.0,
                        "Recovery output follows the baseline preset cutoff");
        }
    };
    const double highWeight = 1.0 / (1.0 + 1'000'000'000'000.0);
    require(6.0 * highWeight < deep::kProcessedMaterialComparisonEpsilon,
            "independent high-stockpile Recovery fixture is below the cutoff");
    run(1'000'000'000'000.0);
    run(0.0);
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

void test_zero_capacity_shipyard_order_waits_and_recovers() {
    deep::GameState state = deep::createHomeSystemScenario();
    state.colonies.front().shipyardCapacity = 0.0;
    state.colonies.front().processorCapacity = 0.0;
    const deep::ColonyId colonyId = state.colonies.front().id;
    const deep::ShipClassId shipClassId = state.shipClasses.front().id;
    const auto initialMaterials = state.colonies.front().processedStockpile;
    deep::Simulation sim{std::move(state)};

    const auto result = sim.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId, .shipClassId = shipClassId, .quantity = 1
    });
    require(result.ok, "zero-capacity colony accepts valid build intent");
    require(sim.state().shipyardOrders.size() == 1, "exactly one waiting order is created");
    const deep::ShipyardOrderId orderId = sim.state().shipyardOrders.front().id;
    require(sim.state().shipyardOrders.front().colonyId == colonyId &&
            sim.state().shipyardOrders.front().shipClassId == shipClassId &&
            sim.state().shipyardOrders.front().quantityRequested == 1 &&
            sim.state().shipyardOrders.front().quantityCompleted == 0 &&
            sim.state().shipyardOrders.front().accumulatedBuildPoints == 0.0 &&
            sim.state().shipyardOrders.front().status == deep::ShipyardOrderStatus::Active,
            "waiting order preserves intent without physical progress");
    require(sim.state().eventLog.size() == 1 &&
            std::holds_alternative<deep::ShipyardOrderCreatedEvent>(sim.state().eventLog.front().payload),
            "accepted intent emits only a creation event");

    sim.advanceDays(30);
    require(sim.state().shipyardOrders.front().id == orderId &&
            sim.state().shipyardOrders.front().quantityCompleted == 0 &&
            sim.state().shipyardOrders.front().accumulatedBuildPoints == 0.0 &&
            sim.state().shipyardOrders.front().status == deep::ShipyardOrderStatus::Active,
            "zero capacity leaves the same order waiting for thirty days");
    require(sim.state().ships.empty() && sim.state().fleets.empty(),
            "zero capacity cannot create a ship or fleet");
    require(sim.state().eventLog.size() == 1, "routine waiting emits no daily warnings");
    for (std::size_t i = 0; i < deep::processedMaterialCount(); ++i) {
        requireNear(sim.state().colonies.front().processedStockpile.amount[i], initialMaterials.amount[i],
                    "idle yard consumes no processed material");
    }

    deep::GameState resumedState = sim.state();
    resumedState.colonies.front().shipyardCapacity = 100.0;
    deep::Simulation resumed{std::move(resumedState)};
    resumed.advanceDays(1);
    require(resumed.state().shipyardOrders.size() == 1 && resumed.state().shipyardOrders.front().id == orderId,
            "capacity recovery uses the existing order");
    require(resumed.state().shipyardOrders.front().accumulatedBuildPoints > 0.0,
            "capacity recovery begins physical progress");
    resumed.advanceDays(4);
    require(resumed.state().shipyardOrders.front().status == deep::ShipyardOrderStatus::Completed &&
            resumed.state().ships.size() == 1, "recovered order completes under existing build rules");
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
    const int etaDays = sim.state().fleets.front().activeOrder.daysRemaining;
    const double expectedFuelCost = sim.state().fleets.front().activeOrder.transitDistanceKm / deep::kKilometersPerMapUnit;
    requireNear(sim.state().ships.front().fuel,
                startingFuel - expectedFuelCost,
                "starting a Terra-to-Mars move consumes planned transit fuel immediately");
    require(sim.state().fleets.front().activeOrder.arrivalDay > sim.state().fleets.front().activeOrder.departureDay,
            "movement stores a projected arrival day");

    sim.advanceDays(etaDays);

    require(sim.state().fleets.front().currentBodyId == marsId, "fleet arrives at Mars after sustained-burn ETA");
    require(sim.state().fleets.front().activeOrder.type == deep::FleetOrderType::None,
            "fleet clears active order after arrival");
    require(std::holds_alternative<deep::FleetArrivedEvent>(sim.state().eventLog.back().payload),
            "fleet arrival remains in event log");
}


void test_fleet_transit_curve_bends_on_expected_display_side() {
    // Verifies that planned sustained-burn routes bend on the UI-selected side
    // of the departure-to-arrival chord. This prevents regression to the visually
    // inverted curve that made interplanetary travel arc the wrong way.
    deep::Simulation sim{deep::createHomeSystemScenario()};

    const deep::ColonyId colonyId = sim.state().colonies.front().id;
    const deep::ShipClassId shipClassId = sim.state().shipClasses.front().id;
    const deep::BodyId marsId = sim.state().bodies.at(1).id;

    require(sim.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order accepted before route-curve test");
    sim.advanceDays(5);

    require(sim.execute(deep::MoveFleetCommand{
        .fleetId = sim.state().fleets.front().id,
        .destinationBodyId = marsId
    }).ok, "move order accepted before route-curve test");

    const deep::FleetOrder& order = sim.state().fleets.front().activeOrder;
    const double chordX = order.projectedArrivalPosition.x - order.departurePosition.x;
    const double chordY = order.projectedArrivalPosition.y - order.departurePosition.y;
    const double midX = (order.departurePosition.x + order.projectedArrivalPosition.x) * 0.5;
    const double midY = (order.departurePosition.y + order.projectedArrivalPosition.y) * 0.5;
    const double offsetX = order.routeCurveControlPoint.x - midX;
    const double offsetY = order.routeCurveControlPoint.y - midY;
    const double cross = (chordX * offsetY) - (chordY * offsetX);

    require(cross < 0.0, "route curve bends on the expected display side of the chord");
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

    const double expectedFuelCost = sim.state().fleets.front().activeOrder.transitDistanceKm / deep::kKilometersPerMapUnit * 0.9;
    requireNear(sim.state().ships.front().fuel,
                1000.0 - expectedFuelCost,
                "fleet commander capped modifier reduces planned transit fuel cost");
}

void test_resource_survey_increases_deposit_confidence() {
    // Verifies that a fleet stationed at a survey target can turn uncertain
    // resource intelligence into higher-confidence reserve estimates.
    deep::GameState state = deep::createHomeSystemScenario();
    const deep::BodyId frontierId = bodyIdByName(state, "Helios Far Survey Object");
    const deep::FleetId fleetId = addTestFleetAt(state, frontierId);
    deep::Simulation sim{std::move(state)};

    double beforeConfidenceTotal = 0.0;
    for (const deep::MineralDeposit& deposit : sim.state().mineralDeposits) {
        if (deposit.bodyId == frontierId) {
            beforeConfidenceTotal += deposit.confidence;
        }
    }

    const auto result = sim.execute(deep::ResourceSurveyCommand{
        .fleetId = fleetId,
        .bodyId = frontierId
    });

    require(result.ok, "resource survey command is accepted at the fleet's current body");
    double afterConfidenceTotal = 0.0;
    bool revealedHiddenDeposit = false;
    for (const deep::MineralDeposit& deposit : sim.state().mineralDeposits) {
        if (deposit.bodyId != frontierId) {
            continue;
        }
        afterConfidenceTotal += deposit.confidence;
        if (deposit.confidence >= deep::kResourceSurveyMinimumRevealedConfidence) {
            revealedHiddenDeposit = true;
        }
    }

    require(afterConfidenceTotal > beforeConfidenceTotal, "survey increases total target-body confidence");
    require(revealedHiddenDeposit, "survey reveals hidden deposits as estimated reserves");
    require(std::holds_alternative<deep::ResourceSurveyCompletedEvent>(sim.state().eventLog.back().payload),
            "accepted survey emits a resource-survey completion event");
}

void test_resource_survey_completes_on_fully_known_body_without_new_information() {
    // A requested pass is valid even when existing records are already known.
    // It must preserve confidence and report an explicit zero-information result.
    deep::GameState state = deep::createHomeSystemScenario();
    const deep::BodyId terraId = bodyIdByName(state, "Terra");
    const deep::FleetId fleetId = addTestFleetAt(state, terraId);
    deep::Simulation sim{std::move(state)};

    std::vector<double> before;
    for (const deep::MineralDeposit& deposit : sim.state().mineralDeposits) {
        if (deposit.bodyId == terraId) {
            before.push_back(deposit.confidence);
        }
    }

    const auto result = sim.execute(deep::ResourceSurveyCommand{
        .fleetId = fleetId,
        .bodyId = terraId
    });

    require(result.ok, "surveying a fully known body completes");
    require(result.message.find("No new information from this pass") != std::string::npos,
            "command result describes the zero-information completion");
    std::size_t index = 0;
    for (const deep::MineralDeposit& deposit : sim.state().mineralDeposits) {
        if (deposit.bodyId == terraId) {
            requireNear(deposit.confidence, before.at(index++), "fully known deposit confidence is unchanged");
        }
    }
    const auto* completed = std::get_if<deep::ResourceSurveyCompletedEvent>(&sim.state().eventLog.back().payload);
    require(completed != nullptr && completed->depositsImproved == 0,
            "fully known body records one zero-information survey result");
    requireNear(completed->averageConfidenceBefore, 0.0, "empty result has zero before average");
    requireNear(completed->averageConfidenceAfter, 0.0, "empty result has zero after average");
}

void test_resource_survey_completes_on_body_without_deposits() {
    // An empty deposit collection models a barren visit without treating the
    // absence of a confidence update as invalid physical survey work.
    deep::GameState state = deep::createHomeSystemScenario();
    const deep::BodyId terraId = bodyIdByName(state, "Terra");
    const deep::FleetId fleetId = addTestFleetAt(state, terraId);
    state.mineralDeposits.clear();
    deep::Simulation sim{std::move(state)};

    const auto result = sim.execute(deep::ResourceSurveyCommand{.fleetId = fleetId, .bodyId = terraId});
    require(result.ok, "surveying a body without deposits completes");
    const auto* completed = std::get_if<deep::ResourceSurveyCompletedEvent>(&sim.state().eventLog.back().payload);
    require(completed != nullptr && completed->depositsImproved == 0,
            "barren visit records one zero-information survey result");
    requireNear(completed->averageConfidenceBefore, 0.0, "barren visit has zero before average");
    requireNear(completed->averageConfidenceAfter, 0.0, "barren visit has zero after average");
}

void test_resource_survey_rejects_invalid_targets() {
    // Command validation should fail before mutating deposits when a UI or save
    // boundary supplies stale fleet/body IDs.
    deep::GameState state = deep::createHomeSystemScenario();
    const deep::BodyId frontierId = bodyIdByName(state, "Helios Far Survey Object");
    const deep::FleetId fleetId = addTestFleetAt(state, frontierId);
    deep::Simulation sim{std::move(state)};

    const auto missingFleet = sim.execute(deep::ResourceSurveyCommand{
        .fleetId = deep::FleetId{999'999},
        .bodyId = frontierId
    });
    require(!missingFleet.ok, "survey rejects a missing fleet ID");

    const auto missingBody = sim.execute(deep::ResourceSurveyCommand{
        .fleetId = fleetId,
        .bodyId = deep::BodyId{999'999}
    });
    require(!missingBody.ok, "survey rejects a missing body ID");
}

void test_resource_survey_rejects_fleet_not_at_body() {
    // V1 surveys are intentionally local and immediate. Fleets must first move
    // to the target body before improving its deposit confidence.
    deep::GameState state = deep::createHomeSystemScenario();
    const deep::BodyId terraId = bodyIdByName(state, "Terra");
    const deep::BodyId frontierId = bodyIdByName(state, "Helios Far Survey Object");
    const deep::FleetId fleetId = addTestFleetAt(state, terraId);
    deep::Simulation sim{std::move(state)};

    const auto result = sim.execute(deep::ResourceSurveyCommand{
        .fleetId = fleetId,
        .bodyId = frontierId
    });

    require(!result.ok, "survey rejects fleets that are not at the target body");
    for (const deep::MineralDeposit& deposit : sim.state().mineralDeposits) {
        if (deposit.bodyId == frontierId && deposit.mineral == deep::Mineral::RareEarthElements) {
            requireNear(deposit.confidence, 0.0, "rejected remote survey leaves hidden deposit hidden");
        }
    }
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

    const int initialEtaDays = sim.state().fleets.front().activeOrder.daysRemaining;
    sim.advanceDays(2);
    require(sim.state().fleets.front().activeOrder.daysRemaining == initialEtaDays - 2,
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

    sim.advanceDays(initialEtaDays);
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
    const int firstEtaDays = sim.state().fleets.front().activeOrder.daysRemaining;
    sim.advanceDays(firstEtaDays);

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

    const int secondEtaDays = sim.state().fleets.front().activeOrder.daysRemaining;
    sim.advanceDays(secondEtaDays);
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

    const int activeEtaDays = sim.state().fleets.front().activeOrder.daysRemaining;
    sim.advanceDays(activeEtaDays);
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
        test_processing_policy_rejects_aggregate_overflow_without_gameplay_mutation();
        test_single_maximum_manual_weight_produces_finite_output();
        test_stockpile_recovery_preserves_active_preset_cutoff();
        test_shipyard_completion();
        test_zero_capacity_shipyard_order_waits_and_recovers();
        test_shipyard_capacity_is_shared_by_fifo_orders();
        test_shipyard_temporary_processed_material_shortage_recovers();
        test_fleet_movement();
        test_fleet_transit_curve_bends_on_expected_display_side();
        test_fleet_movement_rejects_insufficient_fuel();
        test_fleet_commander_reduces_move_fuel_cost_within_cap();
        test_resource_survey_increases_deposit_confidence();
        test_resource_survey_completes_on_fully_known_body_without_new_information();
        test_resource_survey_completes_on_body_without_deposits();
        test_resource_survey_rejects_invalid_targets();
        test_resource_survey_rejects_fleet_not_at_body();
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
