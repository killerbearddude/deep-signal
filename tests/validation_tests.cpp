#include "sim/Commands.h"
#include "sim/GameStateValidation.h"
#include "sim/Minerals.h"
#include "sim/ScenarioFactory.h"
#include "sim/Simulation.h"

// Direct regression tests for GameStateValidation.
// Save/load tests exercise validation through SQLite, but future scenario
// generators, importers, migration tools, and debug fixtures can construct
// GameState directly. These tests protect that trust boundary without using the
// persistence layer.

#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

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

// Builds a non-trivial valid state with one completed order, one ship, one
// fleet, and event history. Many validation invariants involve references among
// these records, so using the real simulation path avoids hand-built fixtures.
deep::GameState makeCompletedPrototypeState() {
    deep::Simulation sim{deep::createHomeSystemScenario()};

    const deep::ColonyId colonyId = sim.state().colonies.front().id;
    const deep::ShipClassId shipClassId = sim.state().shipClasses.front().id;
    require(sim.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    }).ok, "build order setup is accepted");

    sim.advanceDays(5);
    return sim.state();
}

// Verifies that an intentionally malformed in-memory GameState is rejected.
// The optional label is only diagnostic context for CTest output.
template <typename Mutator>
void expectInvalidState(const std::string_view label, Mutator&& mutator) {
    deep::GameState state = makeCompletedPrototypeState();
    mutator(state);

    try {
        deep::validateGameState(state);
    } catch (const std::runtime_error&) {
        return;
    }

    throw TestFailure{std::string{"expected invalid GameState was accepted: "} + std::string{label}};
}

void test_valid_completed_state_is_accepted() {
    // Confirms the shared fixture is valid before individual tests corrupt it.
    // Without this baseline, later negative tests could pass for the wrong reason.
    const deep::GameState state = makeCompletedPrototypeState();
    deep::validateGameState(state);
}

void test_duplicate_body_ids_are_rejected() {
    // Duplicate IDs would make typed references ambiguous for both simulation
    // lookup and persistence round-tripping.
    expectInvalidState("duplicate body IDs", [](deep::GameState& state) {
        state.bodies.at(1).id = state.bodies.at(0).id;
    });
}

void test_stale_id_counters_are_rejected() {
    // Allocators must stay ahead of loaded IDs so the next simulated creation
    // cannot reuse an existing record ID.
    expectInvalidState("stale ship counter", [](deep::GameState& state) {
        state.ids.nextShipId = state.ships.front().id.value;
    });
}

void test_negative_colony_mines_are_rejected() {
    // Production values must be finite and non-negative; otherwise daily economy
    // ticks and forecasts can invert resource flow.
    expectInvalidState("negative colony mines", [](deep::GameState& state) {
        state.colonies.front().mines = -1.0;
    });
}

void test_non_finite_stockpile_amounts_are_rejected() {
    // NaN/Inf quantities poison arithmetic and comparisons. Reject them at the
    // state boundary instead of attempting to repair downstream systems.
    expectInvalidState("non-finite stockpile", [](deep::GameState& state) {
        state.colonies.front().stockpile.set(deep::Mineral::Iron,
                                             std::numeric_limits<double>::infinity());
    });
}

void test_ship_missing_from_owning_fleet_is_rejected() {
    // Ship and fleet references must be bidirectional. A ship whose fleet does
    // not list it would disappear from fleet-level views and order resolution.
    expectInvalidState("ship missing from fleet", [](deep::GameState& state) {
        state.fleets.front().shipIds.clear();
    });
}

void test_fleet_listing_nonexistent_ship_is_rejected() {
    // Fleets must not retain dangling ship IDs. This protects future UI/query
    // layers that will trust fleet summaries to enumerate real ships.
    expectInvalidState("fleet lists missing ship", [](deep::GameState& state) {
        state.fleets.front().shipIds.push_back(deep::ShipId{999});
    });
}

void test_ship_claimed_by_multiple_fleets_is_rejected() {
    // A ship can only occupy one fleet roster. This prevents a corrupted save or
    // importer from making two fleets claim the same physical ship.
    expectInvalidState("ship claimed by multiple fleets", [](deep::GameState& state) {
        const deep::ShipId shipId = state.ships.front().id;
        const deep::BodyId bodyId = state.fleets.front().currentBodyId;

        const deep::FleetId secondFleetId{state.ids.nextFleetId++};
        state.fleets.push_back(deep::Fleet{
            .id = secondFleetId,
            .name = "Duplicate Claim Fleet",
            .currentBodyId = bodyId,
            .destinationBodyId = std::nullopt,
            .shipIds = {shipId},
            .activeOrder = {},
        });
    });
}

void test_active_fleet_order_without_destination_is_rejected() {
    // MoveToBody orders require both a destination field and a target body. An
    // active order without either cannot complete deterministically.
    expectInvalidState("active fleet order without destination", [](deep::GameState& state) {
        deep::Fleet& fleet = state.fleets.front();
        fleet.activeOrder.type = deep::FleetOrderType::MoveToBody;
        fleet.destinationBodyId = std::nullopt;
        fleet.activeOrder.targetBodyId = std::nullopt;
        fleet.activeOrder.daysRemaining = 3;
    });
}

void test_event_day_after_current_day_is_rejected() {
    // Event history must not describe the future relative to the current game
    // date. Save files with future events break chronology and auditability.
    expectInvalidState("future event day", [](deep::GameState& state) {
        state.eventLog.front().day = state.date.day + 1;
    });
}

void test_event_ids_out_of_order_are_rejected() {
    // Events are stored in strict ID order so replay/debug views can treat the
    // log as chronological append-only history.
    expectInvalidState("event IDs out of order", [](deep::GameState& state) {
        require(state.eventLog.size() >= 2, "fixture has at least two events");
        std::swap(state.eventLog.at(0).id, state.eventLog.at(1).id);
    });
}

void test_completed_order_with_build_progress_is_rejected() {
    // Completed shipyard orders are terminal snapshots. Retained build points on
    // completed orders would make the next production tick ambiguous.
    expectInvalidState("completed order retains build progress", [](deep::GameState& state) {
        state.shipyardOrders.front().accumulatedBuildPoints = 1.0;
    });
}

} // namespace

int main() {
    try {
        test_valid_completed_state_is_accepted();
        test_duplicate_body_ids_are_rejected();
        test_stale_id_counters_are_rejected();
        test_negative_colony_mines_are_rejected();
        test_non_finite_stockpile_amounts_are_rejected();
        test_ship_missing_from_owning_fleet_is_rejected();
        test_fleet_listing_nonexistent_ship_is_rejected();
        test_ship_claimed_by_multiple_fleets_is_rejected();
        test_active_fleet_order_without_destination_is_rejected();
        test_event_day_after_current_day_is_rejected();
        test_event_ids_out_of_order_are_rejected();
        test_completed_order_with_build_progress_is_rejected();
    } catch (const std::exception& ex) {
        std::cerr << "Validation test failure: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "All Deep Signal validation tests passed.\n";
    return EXIT_SUCCESS;
}
