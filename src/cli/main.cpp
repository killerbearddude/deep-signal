#include "sim/Commands.h"
#include "sim/Events.h"
#include "sim/Minerals.h"
#include "sim/ScenarioFactory.h"
#include "sim/Simulation.h"

// CLI smoke runner for the headless simulation.
// This executable is a developer-facing verification tool, not the final UI. It
// exercises build, advance-time, fleet movement, and event printing paths.
// Owns a local Simulation directly; it does not exercise app-service or SQLite
// workflows and does not write save files.

#include <iostream>
#include <string_view>
#include <variant>
#include <vector>

namespace {

// Visitor used to print typed event payloads without converting the event model
// into unstructured strings inside the simulation core.
struct EventPrinter {
    void operator()(const deep::MineralExtractedEvent& event) const {
        std::cout << "  Mineral extracted: colony=" << event.colonyId.value
                  << " mineral=" << deep::toString(event.mineral)
                  << " amount=" << event.amount << '\n';
    }

    void operator()(const deep::ShipyardOrderCreatedEvent& event) const {
        std::cout << "  Shipyard order created: order=" << event.orderId.value
                  << " quantity=" << event.quantity << '\n';
    }

    void operator()(const deep::ShipCompletedEvent& event) const {
        std::cout << "  Ship completed: ship=" << event.shipId.value
                  << " fleet=" << event.fleetId.value << '\n';
    }

    void operator()(const deep::FleetOrderAssignedEvent& event) const {
        std::cout << "  Fleet order assigned: fleet=" << event.fleetId.value
                  << " destination=" << event.destinationBodyId.value
                  << " eta_days=" << event.daysRemaining << '\n';
    }

    void operator()(const deep::FleetArrivedEvent& event) const {
        std::cout << "  Fleet arrived: fleet=" << event.fleetId.value
                  << " body=" << event.destinationBodyId.value << '\n';
    }

    void operator()(const deep::ResourceSurveyCompletedEvent& event) const {
        std::cout << "  Resource survey completed: fleet=" << event.fleetId.value
                  << " body=" << event.bodyId.value
                  << " deposits=" << event.depositsImproved << '\n';
    }

    void operator()(const deep::CommandRejectedEvent& event) const {
        std::cout << "  Command rejected: " << event.reason << '\n';
    }
};

// Prints the events returned by time advancement. Events appended by execute()
// live in the state log and are not automatically part of these returned batches.
void printEvents(const std::vector<deep::SimEvent>& events) {
    for (const deep::SimEvent& event : events) {
        std::cout << "Day " << event.day << " event " << event.id.value << ':' << '\n';
        std::visit(EventPrinter{}, event.payload);
    }
}

} // namespace

// Runs the deterministic smoke scenario. Rejected commands and a missing built
// fleet return non-zero; the final location is printed but not asserted. Use the
// regression tests for arrival correctness; exit zero alone does not prove arrival.
int main() {
    deep::Simulation sim{deep::createHomeSystemScenario()};

    const deep::ColonyId colonyId = sim.state().colonies.front().id;
    const deep::ShipClassId shipClassId = sim.state().shipClasses.front().id;

    const auto buildResult = sim.execute(deep::AssignShipyardBuildCommand{
        .colonyId = colonyId,
        .shipClassId = shipClassId,
        .quantity = 1
    });

    if (!buildResult.ok) {
        std::cerr << "Failed to assign build order: " << buildResult.message << '\n';
        return 1;
    }

    std::vector<deep::SimEvent> events = sim.advanceDays(5);
    printEvents(events);

    if (sim.state().fleets.empty()) {
        std::cerr << "Expected shipyard to create one fleet after five days.\n";
        return 1;
    }

    const deep::FleetId fleetId = sim.state().fleets.front().id;
    // Scenario-specific ordering: Terra is first and Mars second. Resolve by an
    // explicit scenario identifier before allowing alternate scenarios here.
    const deep::BodyId marsId = sim.state().bodies.at(1).id;

    const auto moveResult = sim.execute(deep::MoveFleetCommand{
        .fleetId = fleetId,
        .destinationBodyId = marsId
    });

    if (!moveResult.ok) {
        std::cerr << "Failed to assign move order: " << moveResult.message << '\n';
        return 1;
    }

    events = sim.advanceDays(5);
    printEvents(events);

    std::cout << "\nDeep Signal smoke run complete\n";
    std::cout << "Day: " << sim.state().date.day << '\n';
    std::cout << "Ships: " << sim.state().ships.size() << '\n';
    std::cout << "Fleets: " << sim.state().fleets.size() << '\n';
    std::cout << "Fleet body: " << sim.state().fleets.front().currentBodyId.value << '\n';
    std::cout << "Event log: " << sim.state().eventLog.size() << '\n';

    return 0;
}
