#include "sim/ScenarioFactory.h"

// Builds the deterministic home-system scenario used by Phase 1 tests and CLI.
// The data is intentionally hardcoded until the simulation contracts stabilize;
// later phases can replace these values with JSON/TOML definitions.

namespace deep {

GameState createHomeSystemScenario() {
    GameState state;
    state.date.day = 0;

    // IDs are allocated through the same counters the live simulation uses so
    // save/load can later preserve deterministic continuation behavior.
    const StarSystemId solId{state.ids.nextStarSystemId++};
    const BodyId terraId{state.ids.nextBodyId++};
    const BodyId marsId{state.ids.nextBodyId++};
    const ColonyId terraColonyId{state.ids.nextColonyId++};
    const ShipClassId surveyCutterId{state.ids.nextShipClassId++};

    state.starSystems.push_back(StarSystem{
        .id = solId,
        .name = "Sol"
    });

    state.bodies.push_back(Body{
        .id = terraId,
        .systemId = solId,
        .name = "Terra",
        .type = BodyType::Terrestrial,
        .x = 0.0,
        .y = 0.0
    });

    state.bodies.push_back(Body{
        .id = marsId,
        .systemId = solId,
        .name = "Mars",
        .type = BodyType::Terrestrial,
        .x = 240.0,
        .y = 0.0
    });

    MineralSet startingStockpile;
    startingStockpile.set(Mineral::Structural, 10'000.0);
    startingStockpile.set(Mineral::Propulsion, 5'000.0);
    startingStockpile.set(Mineral::Electronics, 5'000.0);
    startingStockpile.set(Mineral::Fuel, 50'000.0);
    startingStockpile.set(Mineral::Ordnance, 2'000.0);

    state.colonies.push_back(Colony{
        .id = terraColonyId,
        .bodyId = terraId,
        .name = "Terra Directorate",
        .stockpile = startingStockpile,
        .mines = 10.0,
        .shipyardCapacity = 100.0
    });

    state.mineralDeposits.push_back(MineralDeposit{
        .bodyId = terraId,
        .mineral = Mineral::Structural,
        .remaining = 1'000'000.0,
        .accessibility = 1.0
    });

    state.mineralDeposits.push_back(MineralDeposit{
        .bodyId = terraId,
        .mineral = Mineral::Propulsion,
        .remaining = 250'000.0,
        .accessibility = 0.45
    });

    MineralSet surveyCutterCost;
    surveyCutterCost.set(Mineral::Structural, 500.0);
    surveyCutterCost.set(Mineral::Propulsion, 120.0);
    surveyCutterCost.set(Mineral::Electronics, 80.0);

    state.shipClasses.push_back(ShipClass{
        .id = surveyCutterId,
        .name = "Survey Cutter",
        .role = ShipRole::Survey,
        .buildCost = surveyCutterCost,
        .buildPoints = 500.0,
        .speedKmPerDay = 50.0,
        .fuelCapacity = 1'000.0
    });

    return state;
}

} // namespace deep
