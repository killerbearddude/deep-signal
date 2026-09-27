#include "save/SaveGameRepository.h"
#include "save/Schema.h"
#include "sim/GameStateValidation.h"
#include "sim/Simulation.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <variant>

using namespace deep;

static GameState makeProofState() {
    GameState state;
    state.ids.nextStarSystemId = 2;
    state.ids.nextBodyId = 2;
    state.ids.nextColonyId = 2;
    state.ids.nextShipClassId = 2;
    state.ids.nextShipyardOrderId = 3;

    state.starSystems.push_back(StarSystem{StarSystemId{1}, "Proof system"});
    state.bodies.push_back(Body{
        .id = BodyId{1}, .systemId = StarSystemId{1}, .name = "Proof body",
        .type = BodyType::Terrestrial, .strategicZone = StrategicZone::InnerCore,
        .parentBodyId = std::nullopt,
        .displayRadius = 8.0
    });
    state.colonies.push_back(Colony{
        .id = ColonyId{1}, .bodyId = BodyId{1}, .name = "Proof shipyard",
        .stockpile = {}, .processedStockpile = {}, .mines = 0.0,
        .processorCapacity = 0.0, .shipyardCapacity = 10.0,
        .processingPolicy = ProcessingPolicy::Balanced,
        .manualProcessingAllocations = {}, .ownerInstitutionId = std::nullopt
    });
    state.shipClasses.push_back(ShipClass{
        .id = ShipClassId{1}, .name = "Proof hull", .role = ShipRole::Survey,
        .buildCost = {},
        .buildPoints = 10.0, .speedKmPerDay = 0.0, .fuelCapacity = 0.0
    });
    // Valid intentionally non-ID-sorted FIFO. Both orders contend for the
    // same one-hull-per-day pool. v10 has no ordinal for this vector.
    state.shipyardOrders.push_back(ShipyardOrder{
        .id = ShipyardOrderId{2}, .colonyId = ColonyId{1},
        .shipClassId = ShipClassId{1}, .quantityRequested = 1,
        .quantityCompleted = 0, .accumulatedBuildPoints = 0.0,
        .status = ShipyardOrderStatus::Active
    });
    state.shipyardOrders.push_back(ShipyardOrder{
        .id = ShipyardOrderId{1}, .colonyId = ColonyId{1},
        .shipClassId = ShipClassId{1}, .quantityRequested = 1,
        .quantityCompleted = 0, .accumulatedBuildPoints = 0.0,
        .status = ShipyardOrderStatus::Active
    });
    validateGameState(state);
    return state;
}

static void printState(const char* label, const GameState& state) {
    std::cout << label << " day=" << state.date.day << " order_ids=";
    for (const auto& order : state.shipyardOrders) {
        std::cout << order.id.value << "(" << order.quantityCompleted << "/"
                  << order.quantityRequested << ",bp=" << order.accumulatedBuildPoints
                  << ") ";
    }
    std::cout << "nextFleetId=" << state.ids.nextFleetId
              << " nextShipId=" << state.ids.nextShipId
              << " nextEventId=" << state.ids.nextEventId << '\n';
}

int main(int argc, char** argv) {
    if (argc != 2) throw std::runtime_error{"expected output SQLite path"};
    const std::filesystem::path path{argv[1]};
    if (save::kSchemaVersion != 10) throw std::runtime_error{"writer is not schema v10"};
    std::filesystem::remove(path);
    const GameState original = makeProofState();
    save::SaveGameRepository::save(path, original);
    const GameState loaded = save::SaveGameRepository::load(path);
    printState("before_save", original);
    printState("after_load", loaded);

    Simulation uninterrupted{original};
    Simulation resumed{loaded};
    const auto expectedEvents = uninterrupted.advanceDays(1);
    const auto actualEvents = resumed.advanceDays(1);
    printState("uninterrupted_day1", uninterrupted.state());
    printState("resumed_day1", resumed.state());
    const auto printEvents = [](const char* label, const std::vector<SimEvent>& events) {
        std::cout << label << " count=" << events.size();
        for (const auto& event : events) {
            if (const auto* completed = std::get_if<ShipCompletedEvent>(&event.payload)) {
                std::cout << " event_id=" << event.id.value
                          << " completed_order=" << completed->orderId.value;
            }
        }
        std::cout << '\n';
    };
    printEvents("uninterrupted_events", expectedEvents);
    printEvents("resumed_events", actualEvents);
    return 0;
}
