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
    startingStockpile.set(Mineral::Iron, 10'000.0);
    startingStockpile.set(Mineral::Nickel, 5'000.0);
    startingStockpile.set(Mineral::Titanium, 4'000.0);
    startingStockpile.set(Mineral::Aluminum, 8'000.0);
    startingStockpile.set(Mineral::Copper, 3'000.0);
    startingStockpile.set(Mineral::Silicon, 4'000.0);
    startingStockpile.set(Mineral::Lithium, 1'000.0);
    startingStockpile.set(Mineral::Uranium, 500.0);
    startingStockpile.set(Mineral::Thorium, 500.0);
    startingStockpile.set(Mineral::RareEarthElements, 700.0);
    startingStockpile.set(Mineral::PlatinumGroupMetals, 200.0);
    startingStockpile.set(Mineral::WaterIce, 50'000.0);
    startingStockpile.set(Mineral::CarbonCompounds, 10'000.0);
    startingStockpile.set(Mineral::Volatiles, 20'000.0);

    ProcessedMaterialSet startingProcessedStockpile;
    startingProcessedStockpile.set(ProcessedMaterial::StructuralAlloys, 1'500.0);
    startingProcessedStockpile.set(ProcessedMaterial::Electronics, 500.0);
    startingProcessedStockpile.set(ProcessedMaterial::Propellant, 2'000.0);
    startingProcessedStockpile.set(ProcessedMaterial::ReactorFuel, 200.0);
    startingProcessedStockpile.set(ProcessedMaterial::IndustrialComposites, 700.0);
    startingProcessedStockpile.set(ProcessedMaterial::OrdnanceMaterials, 300.0);

    state.colonies.push_back(Colony{
        .id = terraColonyId,
        .bodyId = terraId,
        .name = "Terra Directorate",
        .stockpile = startingStockpile,
        .processedStockpile = startingProcessedStockpile,
        .mines = 10.0,
        .processorCapacity = 50.0,
        .shipyardCapacity = 100.0
    });

    // Starter deposits are raw resources only. Colony processors convert them
    // into industrial materials consumed by shipyard construction.
    state.mineralDeposits.push_back(MineralDeposit{
        .bodyId = terraId,
        .mineral = Mineral::Iron,
        .remaining = 1'000'000.0,
        .accessibility = 1.0
    });

    state.mineralDeposits.push_back(MineralDeposit{
        .bodyId = terraId,
        .mineral = Mineral::Nickel,
        .remaining = 600'000.0,
        .accessibility = 0.8
    });

    state.mineralDeposits.push_back(MineralDeposit{
        .bodyId = terraId,
        .mineral = Mineral::Copper,
        .remaining = 200'000.0,
        .accessibility = 0.6
    });

    state.mineralDeposits.push_back(MineralDeposit{
        .bodyId = terraId,
        .mineral = Mineral::Silicon,
        .remaining = 700'000.0,
        .accessibility = 0.9
    });

    state.mineralDeposits.push_back(MineralDeposit{
        .bodyId = terraId,
        .mineral = Mineral::WaterIce,
        .remaining = 2'000'000.0,
        .accessibility = 1.0
    });

    state.mineralDeposits.push_back(MineralDeposit{
        .bodyId = terraId,
        .mineral = Mineral::CarbonCompounds,
        .remaining = 500'000.0,
        .accessibility = 0.7
    });

    state.mineralDeposits.push_back(MineralDeposit{
        .bodyId = terraId,
        .mineral = Mineral::Volatiles,
        .remaining = 800'000.0,
        .accessibility = 0.75
    });

    state.mineralDeposits.push_back(MineralDeposit{
        .bodyId = marsId,
        .mineral = Mineral::Iron,
        .remaining = 800'000.0,
        .accessibility = 0.9
    });

    state.mineralDeposits.push_back(MineralDeposit{
        .bodyId = marsId,
        .mineral = Mineral::Titanium,
        .remaining = 250'000.0,
        .accessibility = 0.55
    });

    state.mineralDeposits.push_back(MineralDeposit{
        .bodyId = marsId,
        .mineral = Mineral::Aluminum,
        .remaining = 300'000.0,
        .accessibility = 0.65
    });

    state.mineralDeposits.push_back(MineralDeposit{
        .bodyId = marsId,
        .mineral = Mineral::WaterIce,
        .remaining = 350'000.0,
        .accessibility = 0.5
    });

    ProcessedMaterialSet surveyCutterCost;
    surveyCutterCost.set(ProcessedMaterial::StructuralAlloys, 250.0);
    surveyCutterCost.set(ProcessedMaterial::Electronics, 80.0);
    surveyCutterCost.set(ProcessedMaterial::Propellant, 150.0);
    surveyCutterCost.set(ProcessedMaterial::ReactorFuel, 20.0);
    surveyCutterCost.set(ProcessedMaterial::IndustrialComposites, 50.0);

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
